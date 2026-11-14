// EasyPSP: DRM presentation on the chosen RG DS Plus panel (implementation in EasypspDrm.h, built into VulkanContext.cpp).
#pragma once
#include "Common/GPU/Vulkan/VulkanLoader.h"

bool EasypspDrm_Wanted();                    // EASYPSP_DRM=1 in the environment
bool EasypspDrm_Open(int screen);            // 0 = top (DSI-2), 1 = bottom (DSI-1); before vkCreateInstance
bool EasypspDrm_Active();
void EasypspDrm_Size(int *w, int *h);
VkSurfaceKHR EasypspDrm_Surface();           // fake surface handle used with the replaced functions
void EasypspDrm_InstallInstanceFunctions();  // after the loader filled the instance function pointers
void EasypspDrm_Close();                     // restore CRTC, drop DRM master
