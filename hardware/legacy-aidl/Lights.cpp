// SPDX-License-Identifier: Apache-2.0

#define LOG_TAG "YoshinoLights"

#include <aidl/android/hardware/light/BnLights.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <hardware/lights.h>
#include <log/log.h>

#include <cstdlib>
#include <map>
#include <string>

using aidl::android::hardware::light::BnLights;
using aidl::android::hardware::light::HwLight;
using aidl::android::hardware::light::HwLightState;
using aidl::android::hardware::light::LightType;

namespace {
constexpr std::pair<LightType, const char*> kLightNames[] = {
    {LightType::BACKLIGHT, LIGHT_ID_BACKLIGHT},
    {LightType::KEYBOARD, LIGHT_ID_KEYBOARD},
    {LightType::BUTTONS, LIGHT_ID_BUTTONS},
    {LightType::BATTERY, LIGHT_ID_BATTERY},
    {LightType::NOTIFICATIONS, LIGHT_ID_NOTIFICATIONS},
    {LightType::ATTENTION, LIGHT_ID_ATTENTION},
    {LightType::BLUETOOTH, LIGHT_ID_BLUETOOTH},
    {LightType::WIFI, LIGHT_ID_WIFI},
};

class Lights final : public BnLights {
  public:
    explicit Lights(const hw_module_t& module) {
        for (const auto& [type, name] : kLightNames) {
            hw_device_t* device = nullptr;
            if (module.methods->open(&module, name, &device) == 0 && device != nullptr) {
                mLights.emplace(type, reinterpret_cast<light_device_t*>(device));
            }
        }
    }

    ~Lights() override {
        for (const auto& it : mLights) {
            it.second->common.close(&it.second->common);
        }
    }

    ndk::ScopedAStatus getLights(std::vector<HwLight>* result) override {
        result->clear();
        for (const auto& it : mLights) {
            HwLight light;
            light.id = static_cast<int32_t>(it.first);
            light.type = it.first;
            result->push_back(light);
        }
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus setLightState(int32_t id, const HwLightState& state) override {
        const auto light = mLights.find(static_cast<LightType>(id));
        if (light == mLights.end() || static_cast<int32_t>(light->first) != id) {
            return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
        }
        light_state_t legacy{};
        legacy.color = static_cast<unsigned int>(state.color);
        legacy.flashMode = static_cast<int>(state.flashMode);
        legacy.flashOnMS = state.flashOnMs;
        legacy.flashOffMS = state.flashOffMs;
        legacy.brightnessMode = static_cast<int>(state.brightnessMode);
        const int status = light->second->set_light(light->second, &legacy);
        if (status != 0) {
            ALOGW("Failed to update light %d: %d", id, status);
        }
        return ndk::ScopedAStatus::ok();
    }

  private:
    std::map<LightType, light_device_t*> mLights;
};
}  // namespace

int main() {
    const hw_module_t* module = nullptr;
    if (hw_get_module(LIGHTS_HARDWARE_MODULE_ID, &module) != 0 ||
        module == nullptr || module->methods == nullptr || module->methods->open == nullptr) {
        ALOGE("Unable to load the legacy lights driver");
        return EXIT_FAILURE;
    }
    ABinderProcess_setThreadPoolMaxThreadCount(0);
    const auto lights = ndk::SharedRefBase::make<Lights>(*module);
    const std::string name = std::string(BnLights::descriptor) + "/default";
    if (AServiceManager_addService(lights->asBinder().get(), name.c_str()) != STATUS_OK) {
        ALOGE("Unable to register %s", name.c_str());
        return EXIT_FAILURE;
    }
    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;
}
