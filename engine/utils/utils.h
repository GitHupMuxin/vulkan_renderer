#pragma once

namespace engine::utils
{
    #define LOAD_INSTANCE_FUNC(instance, funcName, varName) \
    varName = (PFN_vk##funcName)vkGetInstanceProcAddr(instance, "vk" #funcName)
    
    
    #define LOAD_DEVICE_FUNC(instance, funcName, varName) \
    varName = (PFN_vk##funcName)vkGetDeviceProcAddr(instance, "vk" #funcName)
}


