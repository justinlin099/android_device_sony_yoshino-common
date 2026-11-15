// SPDX-License-Identifier: Apache-2.0

#define LOG_TAG "YoshinoMemtrack"

#include <aidl/android/hardware/memtrack/BnMemtrack.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <hardware/memtrack.h>
#include <log/log.h>

#include <cstdlib>
#include <string>

using aidl::android::hardware::memtrack::BnMemtrack;
using aidl::android::hardware::memtrack::DeviceInfo;
using aidl::android::hardware::memtrack::MemtrackRecord;
using aidl::android::hardware::memtrack::MemtrackType;

namespace {
class Memtrack final : public BnMemtrack {
  public:
    explicit Memtrack(const memtrack_module_t& module) : mModule(module) {}

    ndk::ScopedAStatus getMemory(int32_t pid, MemtrackType type,
                                 std::vector<MemtrackRecord>* result) override {
        if (pid < 0 || type < MemtrackType::OTHER || type > MemtrackType::CAMERA) {
            return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
        }
        const auto legacyType = static_cast<memtrack_type>(type);
        result->clear();
        size_t count = 0;
        if (mModule.getMemory(&mModule, pid, legacyType, nullptr, &count) != 0) {
            return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
        }
        if (count == 0) {
            return ndk::ScopedAStatus::ok();
        }
        std::vector<memtrack_record> records(count);
        if (mModule.getMemory(&mModule, pid, legacyType, records.data(), &count) != 0 ||
            count > records.size()) {
            return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
        }
        result->reserve(count);
        for (size_t i = 0; i < count; ++i) {
            MemtrackRecord record;
            record.flags = static_cast<int32_t>(records[i].flags);
            record.sizeInBytes = static_cast<int64_t>(records[i].size_in_bytes);
            result->push_back(record);
        }
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus getGpuDeviceInfo(std::vector<DeviceInfo>* result) override {
        DeviceInfo gpu;
        gpu.id = 0;
        gpu.name = "kgsl-3d0";
        *result = {gpu};
        return ndk::ScopedAStatus::ok();
    }

  private:
    const memtrack_module_t& mModule;
};
}  // namespace

int main() {
    const hw_module_t* hardware = nullptr;
    if (hw_get_module(MEMTRACK_HARDWARE_MODULE_ID, &hardware) != 0 || hardware == nullptr) {
        ALOGE("Unable to load the legacy memtrack driver");
        return EXIT_FAILURE;
    }
    const auto& module = *reinterpret_cast<const memtrack_module_t*>(hardware);
    if (module.getMemory == nullptr || (module.init != nullptr && module.init(&module) != 0)) {
        ALOGE("Unable to initialize the legacy memtrack driver");
        return EXIT_FAILURE;
    }
    ABinderProcess_setThreadPoolMaxThreadCount(0);
    const auto memtrack = ndk::SharedRefBase::make<Memtrack>(module);
    const std::string name = std::string(BnMemtrack::descriptor) + "/default";
    if (AServiceManager_addService(memtrack->asBinder().get(), name.c_str()) != STATUS_OK) {
        ALOGE("Unable to register %s", name.c_str());
        return EXIT_FAILURE;
    }
    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;
}
