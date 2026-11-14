// EasyPSP: Vulkan swapchain emulated on top of DRM/KMS for the RG DS Plus (Mali-G52, libmali g29p1).
// Mali's VK_KHR_display only sees the first DRM connector (bottom panel), so like EasyCast we present ourselves:
// DRM dumb buffers -> dma-buf -> imported VkImages ("swapchain images"); a present thread waits for the GPU
// and page-flips them on the chosen panel (top DSI-2 or bottom DSI-1). No CPU pixel copies.
// The PPSSPP Vulkan function pointers for surface/swapchain are replaced by the functions below, so the
// renderer itself is unchanged. Enabled with EASYPSP_DRM=1 (set by run.py). Included only from VulkanContext.cpp.
#pragma once
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <dlfcn.h>
#include <pthread.h>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "Common/GPU/Vulkan/VulkanLoader.h"
#include "Common/GPU/Vulkan/EasypspDrmApi.h"

namespace easypsp_drm {
using namespace PPSSPP_VK;

static const VkSurfaceKHR kSurface = (VkSurfaceKHR)(uintptr_t)0xE5A75000;
static const VkSwapchainKHR kSwapchain = (VkSwapchainKHR)(uintptr_t)0xE5A75C00;
static const uint32_t kImages = 3;

struct Buffer { uint32_t handle = 0, fb = 0; VkImage image = VK_NULL_HANDLE; VkDeviceMemory memory = VK_NULL_HANDLE; VkFence fence = VK_NULL_HANDLE; };

struct State {
	bool enabled = false;
	void *lib = nullptr;
	int fd = -1;
	uint32_t connector = 0, crtc = 0;
	drmModeModeInfo mode{};
	drmModeCrtc saved{};
	bool modeset = false, pending = false;
	int screen = 0;
	VkDevice dev = VK_NULL_HANDLE;
	std::vector<Buffer> buffers;
	bool swapchainAlive = false;
	// present thread
	std::mutex m;
	std::condition_variable cv, freed;
	std::deque<uint32_t> jobs;
	std::thread worker;
	bool stopping = false, failed = false;
	bool inUse[8]{};
	int onScreen = -1, pendingIdx = -1;
	uint32_t nextAcquire = 0;
	// real functions
	PFN_vkGetPhysicalDeviceSurfaceSupportKHR rSupport = nullptr;
	PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR rCaps = nullptr;
	PFN_vkGetPhysicalDeviceSurfaceFormatsKHR rFormats = nullptr;
	PFN_vkGetPhysicalDeviceSurfacePresentModesKHR rModes = nullptr;
	PFN_vkCreateSwapchainKHR rCreate = nullptr;
	PFN_vkDestroySwapchainKHR rDestroy = nullptr;
	PFN_vkGetSwapchainImagesKHR rImages = nullptr;
	PFN_vkAcquireNextImageKHR rAcquire = nullptr;
	PFN_vkQueuePresentKHR rPresent = nullptr;
	PFN_vkDestroySurfaceKHR rDestroySurface = nullptr;
};
static State S;

template<class T> static T fn(const char *name) {
	T p = reinterpret_cast<T>(dlsym(S.lib, name));
	if (!p) fprintf(stderr, "EASYPSP_DRM missing %s\n", name);
	return p;
}

static void closeDrm() {
	if (S.fd >= 0) {
		auto drop = fn<int(*)(int)>("drmDropMaster");
		if (drop) drop(S.fd);
		close(S.fd);
		S.fd = -1;
	}
	if (S.lib) { dlclose(S.lib); S.lib = nullptr; }
	S.enabled = false;
}

static void flipped(int, unsigned, unsigned, unsigned, void *p) { *static_cast<bool *>(p) = false; }

static bool waitFlip() {
	if (!S.pending) return true;
	drmEventContext ec{}; ec.version = 2; ec.page_flip_handler = flipped;
	auto handle = fn<int(*)(int, drmEventContext *)>("drmHandleEvent");
	auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while (S.pending) {
		if (std::chrono::steady_clock::now() > until) { fprintf(stderr, "EASYPSP_DRM flip timeout\n"); S.pending = false; return false; }
		pollfd p{ S.fd, POLLIN, 0 };
		int r = poll(&p, 1, 100);
		if (r < 0 && errno == EINTR) continue;
		if (r < 0 || (p.revents & (POLLERR | POLLHUP | POLLNVAL))) { S.pending = false; return false; }
		if (r > 0 && handle(S.fd, &ec)) { S.pending = false; return false; }
	}
	return true;
}

static bool show(uint32_t index) {
	if (!S.modeset) {
		auto set = fn<int(*)(int, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t *, int, drmModeModeInfo *)>("drmModeSetCrtc");
		if (set(S.fd, S.crtc, S.buffers[index].fb, 0, 0, &S.connector, 1, &S.mode)) { fprintf(stderr, "EASYPSP_DRM modeset failed errno=%d\n", errno); return false; }
		S.modeset = true;
		return true;
	}
	auto flip = fn<int(*)(int, uint32_t, uint32_t, uint32_t, void *)>("drmModePageFlip");
	if (flip(S.fd, S.crtc, S.buffers[index].fb, DRM_MODE_PAGE_FLIP_EVENT, &S.pending)) { fprintf(stderr, "EASYPSP_DRM flip failed errno=%d\n", errno); return false; }
	S.pending = true;
	return true;
}

static void worker() {
	pthread_setname_np(pthread_self(), "EasyPSP-present");
	std::unique_lock<std::mutex> lk(S.m);
	for (;;) {
		S.cv.wait(lk, [] { return S.stopping || !S.jobs.empty(); });
		if (S.jobs.empty() && S.stopping) break;
		uint32_t idx = S.jobs.front();
		S.jobs.pop_front();
		lk.unlock();
		bool ok = waitFlip();   // previous flip done -> the image it replaced is free
		lk.lock();
		if (S.pendingIdx >= 0) { if (S.onScreen >= 0) S.inUse[S.onScreen] = false; S.onScreen = S.pendingIdx; S.pendingIdx = -1; }
		S.freed.notify_all();
		lk.unlock();
		if (ok) {
			VkResult r = vkWaitForFences(S.dev, 1, &S.buffers[idx].fence, VK_TRUE, 2000000000ULL);
			if (r != VK_SUCCESS) { fprintf(stderr, "EASYPSP_DRM GPU fence timeout %d\n", r); ok = false; }
		}
		const bool wasModeset = S.modeset;
		if (ok) ok = show(idx);
		lk.lock();
		if (ok && !wasModeset) { if (S.onScreen >= 0) S.inUse[S.onScreen] = false; S.onScreen = (int)idx; }
		else if (ok) S.pendingIdx = (int)idx;
		else { S.inUse[idx] = false; S.failed = true; }
		S.freed.notify_all();
	}
}

static void stopWorker() {
	{
		std::lock_guard<std::mutex> lk(S.m);
		S.stopping = true;
		S.cv.notify_all();
	}
	if (S.worker.joinable()) S.worker.join();
	waitFlip();
	std::lock_guard<std::mutex> lk(S.m);
	S.jobs.clear();
	for (auto &u : S.inUse) u = false;
	S.onScreen = S.pendingIdx = -1;
	S.stopping = false;
	S.failed = false;
	S.freed.notify_all();
}

static void freeBuffers() {
	if (S.worker.joinable()) stopWorker();
	if (S.dev) vkDeviceWaitIdle(S.dev);
	if (S.modeset) {
		auto set = fn<int(*)(int, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t *, int, drmModeModeInfo *)>("drmModeSetCrtc");
		int r = (S.saved.mode_valid && S.saved.buffer_id) ? set(S.fd, S.crtc, S.saved.buffer_id, S.saved.x, S.saved.y, &S.connector, 1, &S.saved.mode)
			: set(S.fd, S.crtc, 0, 0, 0, nullptr, 0, nullptr);
		fprintf(stderr, "EASYPSP_DRM restore=%d errno=%d\n", r, r ? errno : 0);
		S.modeset = false;
	}
	auto rmfb = fn<int(*)(int, uint32_t)>("drmModeRmFB");
	for (auto &b : S.buffers) {
		if (b.fb && rmfb) rmfb(S.fd, b.fb);
		if (b.image) vkDestroyImage(S.dev, b.image, nullptr);
		if (b.memory) vkFreeMemory(S.dev, b.memory, nullptr);
		if (b.fence) vkDestroyFence(S.dev, b.fence, nullptr);
		if (b.handle) { drm_mode_destroy_dumb d{}; d.handle = b.handle; ioctl(S.fd, DRM_IOCTL_MODE_DESTROY_DUMB, &d); }
	}
	S.buffers.clear();
	S.swapchainAlive = false;
	S.dev = VK_NULL_HANDLE;
}

static VkResult createBuffers(VkDevice device, uint32_t count) {
	freeBuffers();
	S.dev = device;
	auto getFdProps = (PFN_vkGetMemoryFdPropertiesKHR)vkGetDeviceProcAddr(device, "vkGetMemoryFdPropertiesKHR");
	auto prime = fn<int(*)(int, uint32_t, uint32_t, int *)>("drmPrimeHandleToFD");
	auto addfb = fn<int(*)(int, uint32_t, uint32_t, uint32_t, const uint32_t *, const uint32_t *, const uint32_t *, const uint64_t *, uint32_t *, uint32_t)>("drmModeAddFB2WithModifiers");
	if (!getFdProps || !prime || !addfb) return VK_ERROR_INITIALIZATION_FAILED;
	S.buffers.resize(count);
	for (auto &b : S.buffers) {
		drm_mode_create_dumb bo{}; bo.width = S.mode.hdisplay; bo.height = S.mode.vdisplay; bo.bpp = 32;
		if (ioctl(S.fd, DRM_IOCTL_MODE_CREATE_DUMB, &bo)) { fprintf(stderr, "EASYPSP_DRM dumb alloc failed\n"); return VK_ERROR_OUT_OF_DEVICE_MEMORY; }
		b.handle = bo.handle;
		VkSubresourceLayout layout{}; layout.rowPitch = bo.pitch;
		VkImageDrmFormatModifierExplicitCreateInfoEXT modifier{ VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT };
		modifier.drmFormatModifier = 0; modifier.drmFormatModifierPlaneCount = 1; modifier.pPlaneLayouts = &layout;
		VkExternalMemoryImageCreateInfo ext{ VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO };
		ext.pNext = &modifier; ext.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
		VkImageCreateInfo ci{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
		ci.pNext = &ext; ci.imageType = VK_IMAGE_TYPE_2D; ci.format = VK_FORMAT_R8G8B8A8_UNORM;
		ci.extent = { S.mode.hdisplay, S.mode.vdisplay, 1 }; ci.mipLevels = 1; ci.arrayLayers = 1;
		ci.samples = VK_SAMPLE_COUNT_1_BIT; ci.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
		ci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE; ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		VkResult res = vkCreateImage(device, &ci, nullptr, &b.image);
		if (res != VK_SUCCESS) { fprintf(stderr, "EASYPSP_DRM create image %d\n", res); return res; }
		VkMemoryRequirements req{}; vkGetImageMemoryRequirements(device, b.image, &req);
		int dma = -1;
		if (prime(S.fd, b.handle, DRM_CLOEXEC | DRM_RDWR, &dma)) { fprintf(stderr, "EASYPSP_DRM prime export failed\n"); return VK_ERROR_INITIALIZATION_FAILED; }
		VkMemoryFdPropertiesKHR props{ VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR };
		res = getFdProps(device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, dma, &props);
		if (res != VK_SUCCESS) { close(dma); fprintf(stderr, "EASYPSP_DRM fd props %d\n", res); return res; }
		uint32_t bits = req.memoryTypeBits & props.memoryTypeBits, type = 0;
		if (!bits || req.size > bo.size) { close(dma); fprintf(stderr, "EASYPSP_DRM memory layout incompatible\n"); return VK_ERROR_INITIALIZATION_FAILED; }
		while (!(bits & (1u << type))) ++type;
		VkMemoryDedicatedAllocateInfo dedicated{ VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO }; dedicated.image = b.image;
		VkImportMemoryFdInfoKHR import{ VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR }; import.pNext = &dedicated;
		import.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT; import.fd = dma;
		VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO }; ai.pNext = &import;
		ai.allocationSize = req.size; ai.memoryTypeIndex = type;
		res = vkAllocateMemory(device, &ai, nullptr, &b.memory);
		if (res != VK_SUCCESS) { close(dma); fprintf(stderr, "EASYPSP_DRM import memory %d\n", res); return res; }
		res = vkBindImageMemory(device, b.image, b.memory, 0);
		if (res != VK_SUCCESS) return res;
		uint32_t handles[4] = { b.handle }, pitches[4] = { bo.pitch }, offsets[4] = { 0 };
		uint64_t mods[4] = { 0 };
		if (addfb(S.fd, S.mode.hdisplay, S.mode.vdisplay, DRM_FORMAT_ABGR8888, handles, pitches, offsets, mods, &b.fb, DRM_MODE_FB_MODIFIERS)) {
			fprintf(stderr, "EASYPSP_DRM AddFB2 failed errno=%d\n", errno); return VK_ERROR_INITIALIZATION_FAILED;
		}
		VkFenceCreateInfo fi{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
		vkCreateFence(device, &fi, nullptr, &b.fence);
	}
	S.nextAcquire = 0;
	S.swapchainAlive = true;
	fprintf(stderr, "EASYPSP_DRM %u imported buffers %ux%u on %s\n", count, S.mode.hdisplay, S.mode.vdisplay, S.screen == 1 ? "bottom" : "top");
	return VK_SUCCESS;
}

// ---- replaced Vulkan entry points ----
static VKAPI_ATTR VkResult VKAPI_CALL Support(VkPhysicalDevice pd, uint32_t q, VkSurfaceKHR s, VkBool32 *out) {
	if (s != kSurface) return S.rSupport(pd, q, s, out);
	*out = VK_TRUE; return VK_SUCCESS;
}
static VKAPI_ATTR VkResult VKAPI_CALL Caps(VkPhysicalDevice pd, VkSurfaceKHR s, VkSurfaceCapabilitiesKHR *c) {
	if (s != kSurface) return S.rCaps(pd, s, c);
	*c = {};
	c->minImageCount = kImages - 1; c->maxImageCount = kImages;
	c->currentExtent = { S.mode.hdisplay, S.mode.vdisplay };
	c->minImageExtent = c->currentExtent; c->maxImageExtent = c->currentExtent;
	c->maxImageArrayLayers = 1;
	c->supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR; c->currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	c->supportedCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	c->supportedUsageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	return VK_SUCCESS;
}
static VKAPI_ATTR VkResult VKAPI_CALL Formats(VkPhysicalDevice pd, VkSurfaceKHR s, uint32_t *n, VkSurfaceFormatKHR *f) {
	if (s != kSurface) return S.rFormats(pd, s, n, f);
	if (!f) { *n = 1; return VK_SUCCESS; }
	if (*n < 1) return VK_INCOMPLETE;
	f[0] = { VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR }; *n = 1;
	return VK_SUCCESS;
}
static VKAPI_ATTR VkResult VKAPI_CALL Modes(VkPhysicalDevice pd, VkSurfaceKHR s, uint32_t *n, VkPresentModeKHR *m) {
	if (s != kSurface) return S.rModes(pd, s, n, m);
	if (!m) { *n = 1; return VK_SUCCESS; }
	if (*n < 1) return VK_INCOMPLETE;
	m[0] = VK_PRESENT_MODE_FIFO_KHR; *n = 1;
	return VK_SUCCESS;
}
static VKAPI_ATTR VkResult VKAPI_CALL CreateSwapchain(VkDevice d, const VkSwapchainCreateInfoKHR *ci, const VkAllocationCallbacks *a, VkSwapchainKHR *out) {
	if (ci->surface != kSurface) return S.rCreate(d, ci, a, out);
	uint32_t count = ci->minImageCount < 2 ? 2 : (ci->minImageCount > 8 ? 8 : ci->minImageCount);
	VkResult r = createBuffers(d, count);
	if (r != VK_SUCCESS) { freeBuffers(); return r; }
	*out = kSwapchain;
	return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL DestroySwapchain(VkDevice d, VkSwapchainKHR sc, const VkAllocationCallbacks *a) {
	if (sc != kSwapchain) { if (sc) S.rDestroy(d, sc, a); return; }
	// The buffers live until the next CreateSwapchain or DestroySurface: PPSSPP destroys the old swapchain
	// after creating the new one, and both have the same fake handle.
}
static VKAPI_ATTR VkResult VKAPI_CALL Images(VkDevice d, VkSwapchainKHR sc, uint32_t *n, VkImage *img) {
	if (sc != kSwapchain) return S.rImages(d, sc, n, img);
	if (!img) { *n = (uint32_t)S.buffers.size(); return VK_SUCCESS; }
	uint32_t c = *n < S.buffers.size() ? *n : (uint32_t)S.buffers.size();
	for (uint32_t i = 0; i < c; i++) img[i] = S.buffers[i].image;
	*n = c;
	return c < S.buffers.size() ? VK_INCOMPLETE : VK_SUCCESS;
}
static VKAPI_ATTR VkResult VKAPI_CALL Acquire(VkDevice d, VkSwapchainKHR sc, uint64_t timeout, VkSemaphore sem, VkFence fence, uint32_t *idx) {
	if (sc != kSwapchain) return S.rAcquire(d, sc, timeout, sem, fence, idx);
	if (!S.swapchainAlive) return VK_ERROR_OUT_OF_DATE_KHR;
	uint32_t i;
	{
		std::unique_lock<std::mutex> lk(S.m);
		i = S.nextAcquire;
		if (!S.freed.wait_for(lk, std::chrono::seconds(2), [&] { return !S.inUse[i] || S.failed; })) return VK_TIMEOUT;
		S.failed = false;
		S.inUse[i] = true;
		S.nextAcquire = (i + 1) % S.buffers.size();
	}
	*idx = i;
	// the image is not used by the GPU or the display: signal the semaphore/fence right away
	if (sem || fence) {
		VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
		si.signalSemaphoreCount = sem ? 1 : 0; si.pSignalSemaphores = &sem;
		VkQueue q = VK_NULL_HANDLE; vkGetDeviceQueue(d, 0, 0, &q);
		vkQueueSubmit(q, 1, &si, fence);
	}
	return VK_SUCCESS;
}
static VKAPI_ATTR VkResult VKAPI_CALL Present(VkQueue q, const VkPresentInfoKHR *pi) {
	if (pi->swapchainCount != 1 || pi->pSwapchains[0] != kSwapchain) return S.rPresent(q, pi);
	uint32_t i = pi->pImageIndices[0];
	Buffer &b = S.buffers[i];
	vkResetFences(S.dev, 1, &b.fence);
	std::vector<VkPipelineStageFlags> stages(pi->waitSemaphoreCount, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
	VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
	si.waitSemaphoreCount = pi->waitSemaphoreCount; si.pWaitSemaphores = pi->pWaitSemaphores; si.pWaitDstStageMask = stages.data();
	VkResult r = vkQueueSubmit(q, 1, &si, b.fence);
	if (r != VK_SUCCESS) return r;
	std::lock_guard<std::mutex> lk(S.m);
	if (!S.worker.joinable()) { S.stopping = false; S.worker = std::thread(worker); }
	S.jobs.push_back(i);
	S.cv.notify_all();
	if (pi->pResults) pi->pResults[0] = VK_SUCCESS;
	return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL DestroySurface(VkInstance inst, VkSurfaceKHR s, const VkAllocationCallbacks *a) {
	if (s != kSurface) { S.rDestroySurface(inst, s, a); return; }
	freeBuffers();
}

}  // namespace easypsp_drm

// ---- public API (EasypspDrmApi.h) ----
bool EasypspDrm_Wanted() {
	const char *e = getenv("EASYPSP_DRM");
	return e && !strcmp(e, "1");
}

bool EasypspDrm_Open(int screen) {
	using namespace easypsp_drm;
	if (S.enabled) return true;
	S.screen = screen == 1 ? 1 : 0;
	S.lib = dlopen("libdrm.so.2", RTLD_NOW | RTLD_LOCAL);
	if (!S.lib) { fprintf(stderr, "EASYPSP_DRM cannot load libdrm\n"); return false; }
	S.fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
	if (S.fd < 0) { fprintf(stderr, "EASYPSP_DRM cannot open card0\n"); closeDrm(); return false; }
	auto master = fn<int(*)(int)>("drmSetMaster");
	auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	int tries = 0;
	while (master(S.fd)) {
		if (std::chrono::steady_clock::now() > until) { fprintf(stderr, "EASYPSP_DRM master busy\n"); closeDrm(); return false; }
		usleep(50000); tries++;
	}
	auto getres = fn<drmModeRes *(*)(int)>("drmModeGetResources");
	auto freeres = fn<void(*)(drmModeRes *)>("drmModeFreeResources");
	auto getcon = fn<drmModeConnector *(*)(int, uint32_t)>("drmModeGetConnector");
	auto freecon = fn<void(*)(drmModeConnector *)>("drmModeFreeConnector");
	auto getenc = fn<drmModeEncoder *(*)(int, uint32_t)>("drmModeGetEncoder");
	auto freeenc = fn<void(*)(drmModeEncoder *)>("drmModeFreeEncoder");
	auto getcrtc = fn<drmModeCrtc *(*)(int, uint32_t)>("drmModeGetCrtc");
	auto freecrtc = fn<void(*)(drmModeCrtc *)>("drmModeFreeCrtc");
	const unsigned wantId = S.screen == 1 ? 1 : 2;   // DSI-1 = bottom, DSI-2 = top
	drmModeRes *r = getres(S.fd);
	if (!r) { closeDrm(); return false; }
	for (int i = 0; i < r->count_connectors && !(S.connector && S.crtc); i++) {
		drmModeConnector *c = getcon(S.fd, r->connectors[i]);
		if (c && c->connector_type == DRM_MODE_CONNECTOR_DSI && c->connector_type_id == wantId && c->connection == DRM_MODE_CONNECTED && c->count_modes) {
			S.connector = c->connector_id; S.mode = c->modes[0];
			for (int j = 0; j < c->count_modes; j++) if (c->modes[j].type & DRM_MODE_TYPE_PREFERRED) { S.mode = c->modes[j]; break; }
			uint32_t eid = c->encoder_id ? c->encoder_id : (c->count_encoders ? c->encoders[0] : 0);
			drmModeEncoder *e = eid ? getenc(S.fd, eid) : nullptr;
			if (e) {
				S.crtc = e->crtc_id;
				for (int j = 0; !S.crtc && j < r->count_crtcs; j++) if (e->possible_crtcs & (1u << j)) S.crtc = r->crtcs[j];
				freeenc(e);
			}
		}
		if (c) freecon(c);
	}
	freeres(r);
	if (!S.connector || !S.crtc) { fprintf(stderr, "EASYPSP_DRM panel DSI-%u not found\n", wantId); closeDrm(); return false; }
	drmModeCrtc *old = getcrtc(S.fd, S.crtc);
	if (old) { S.saved = *old; freecrtc(old); }
	S.enabled = true;
	fprintf(stderr, "EASYPSP_DRM screen=%s connector=%u crtc=%u %ux%u master after %d tries\n", S.screen == 1 ? "bottom" : "top", S.connector, S.crtc, S.mode.hdisplay, S.mode.vdisplay, tries);
	return true;
}

bool EasypspDrm_Active() { return easypsp_drm::S.enabled; }
void EasypspDrm_Size(int *w, int *h) { *w = easypsp_drm::S.mode.hdisplay; *h = easypsp_drm::S.mode.vdisplay; }
VkSurfaceKHR EasypspDrm_Surface() { return easypsp_drm::kSurface; }

void EasypspDrm_InstallInstanceFunctions() {
	using namespace easypsp_drm;
	if (!S.enabled) return;
	S.rSupport = vkGetPhysicalDeviceSurfaceSupportKHR; vkGetPhysicalDeviceSurfaceSupportKHR = Support;
	S.rCaps = vkGetPhysicalDeviceSurfaceCapabilitiesKHR; vkGetPhysicalDeviceSurfaceCapabilitiesKHR = Caps;
	S.rFormats = vkGetPhysicalDeviceSurfaceFormatsKHR; vkGetPhysicalDeviceSurfaceFormatsKHR = Formats;
	S.rModes = vkGetPhysicalDeviceSurfacePresentModesKHR; vkGetPhysicalDeviceSurfacePresentModesKHR = Modes;
	S.rCreate = vkCreateSwapchainKHR; vkCreateSwapchainKHR = CreateSwapchain;
	S.rDestroy = vkDestroySwapchainKHR; vkDestroySwapchainKHR = DestroySwapchain;
	S.rImages = vkGetSwapchainImagesKHR; vkGetSwapchainImagesKHR = Images;
	S.rAcquire = vkAcquireNextImageKHR; vkAcquireNextImageKHR = Acquire;
	S.rPresent = vkQueuePresentKHR; vkQueuePresentKHR = Present;
	S.rDestroySurface = vkDestroySurfaceKHR; vkDestroySurfaceKHR = DestroySurface;
	fprintf(stderr, "EASYPSP_DRM Vulkan surface/swapchain functions replaced\n");
}

void EasypspDrm_Close() {
	using namespace easypsp_drm;
	if (!S.lib) return;
	freeBuffers();
	closeDrm();
}
