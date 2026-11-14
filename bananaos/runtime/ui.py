"""Small installer/status screen using BananaOS SDL; no bundled GUI dependencies."""
import ctypes as c
import os
import select
import struct
import time


class Screen:
    def __init__(self):
        os.environ['SDL_VIDEODRIVER'] = 'dummy'
        os.environ['SDL_BANANA_PANELS'] = 'side'
        self.s = c.CDLL('libSDL2-2.0.so.0')
        self.t = c.CDLL('libSDL2_ttf-2.0.so.0')
        self.window = self.renderer = self.font = None
        self.fd = None
        self.initialized = self.ttf_initialized = False
        P, I = c.c_void_p, c.c_int
        def api(lib, name, result, *args):
            f = getattr(lib, name); f.restype = result; f.argtypes = list(args); return f
        self.init = api(self.s, 'SDL_Init', I, c.c_uint32)
        self.quit = api(self.s, 'SDL_Quit', None)
        self.error = api(self.s, 'SDL_GetError', c.c_char_p)
        self.make_window = api(self.s, 'SDL_CreateWindow', P, c.c_char_p, I, I, I, I, c.c_uint32)
        self.make_renderer = api(self.s, 'SDL_CreateRenderer', P, P, I, c.c_uint32)
        self.color = api(self.s, 'SDL_SetRenderDrawColor', I, P, c.c_uint8, c.c_uint8, c.c_uint8, c.c_uint8)
        self.clear = api(self.s, 'SDL_RenderClear', I, P)
        self.present = api(self.s, 'SDL_RenderPresent', None, P)
        self.destroy_renderer = api(self.s, 'SDL_DestroyRenderer', None, P)
        self.destroy_window = api(self.s, 'SDL_DestroyWindow', None, P)
        self.poll = api(self.s, 'SDL_PollEvent', I, P)
        self.ttf_init = api(self.t, 'TTF_Init', I)
        self.ttf_quit = api(self.t, 'TTF_Quit', None)
        self.open_font = api(self.t, 'TTF_OpenFont', P, c.c_char_p, I)
        self.close_font = api(self.t, 'TTF_CloseFont', None, P)
        class Color(c.Structure):
            _fields_ = [('r', c.c_uint8), ('g', c.c_uint8), ('b', c.c_uint8), ('a', c.c_uint8)]
        class Rect(c.Structure):
            _fields_ = [('x', I), ('y', I), ('w', I), ('h', I)]
        self.Color, self.Rect = Color, Rect
        self.render_text = api(self.t, 'TTF_RenderUTF8_Blended', P, P, c.c_char_p, Color)
        self.texture = api(self.s, 'SDL_CreateTextureFromSurface', P, P, P)
        self.free_surface = api(self.s, 'SDL_FreeSurface', None, P)
        self.destroy_texture = api(self.s, 'SDL_DestroyTexture', None, P)
        self.query = api(self.s, 'SDL_QueryTexture', I, P, P, P, c.POINTER(I), c.POINTER(I))
        self.copy = api(self.s, 'SDL_RenderCopy', I, P, P, P, c.POINTER(Rect))
        try:
            if self.init(0x20): raise RuntimeError(self.error().decode(errors='replace'))
            self.initialized = True
            if self.ttf_init(): raise RuntimeError('SDL_ttf initialization failed')
            self.ttf_initialized = True
            self.window = self.make_window(b'EasyPSP Installer', 0, 0, 2048, 768, 1)
            if not self.window: raise RuntimeError('Installer window failed')
            self.renderer = self.make_renderer(self.window, -1, 1) or self.make_renderer(self.window, -1, 0)
            if not self.renderer: raise RuntimeError('Installer renderer failed')
            self.font = self.open_font(b'/usr/share/banana/banana/fonts/Inter-Regular.ttf', 27)
            if not self.font: raise RuntimeError('BananaOS font not found')
            self.fd = os.open('/dev/input/js0', os.O_RDONLY | os.O_NONBLOCK)
            while select.select([self.fd], [], [], 0)[0]: os.read(self.fd, 4096)
        except BaseException:
            self.close(); raise

    def draw(self, title, lines, footer='A / B: return to Apps'):
        self.color(self.renderer, 14, 22, 35, 255); self.clear(self.renderer)
        for x in (48, 1072):
            for i, line in enumerate([title, ''] + list(lines)):
                if i > 13: break
                surf = self.render_text(self.font, line.encode('utf-8'), self.Color(245, 205, 120, 255) if i == 0 else self.Color(230, 235, 243, 255))
                if not surf: continue
                tex = self.texture(self.renderer, surf); self.free_surface(surf)
                if not tex: continue
                w, h = c.c_int(), c.c_int(); self.query(tex, None, None, c.byref(w), c.byref(h))
                rect = self.Rect(x, 38 + i * 43, min(w.value, 930), h.value)
                self.copy(self.renderer, tex, None, c.byref(rect)); self.destroy_texture(tex)
            surf = self.render_text(self.font, footer.encode(), self.Color(170, 192, 211, 255))
            if surf:
                tex = self.texture(self.renderer, surf); self.free_surface(surf)
                if tex:
                    w, h = c.c_int(), c.c_int(); self.query(tex, None, None, c.byref(w), c.byref(h))
                    rect = self.Rect(x, 700, min(w.value, 930), h.value)
                    self.copy(self.renderer, tex, None, c.byref(rect)); self.destroy_texture(tex)
        self.present(self.renderer)

    def button(self, timeout=.05):
        event = c.create_string_buffer(64)
        while self.poll(event):
            if int.from_bytes(event.raw[:4], 'little') == 0x100: return 'cancel'
        if select.select([self.fd], [], [], timeout)[0]:
            data = os.read(self.fd, 4096)
            for off in range(0, len(data) - 7, 8):
                _, value, kind, number = struct.unpack_from('IhBB', data, off)
                if kind == 1 and value == 1:
                    if number == 1: return 'accept'  # physical Nintendo A, right
                    if number == 0: return 'cancel'  # physical Nintendo B, bottom
        return None

    def wait(self):
        while not self.button(): pass

    def close(self):
        if self.fd is not None: os.close(self.fd); self.fd = None
        if self.font: self.close_font(self.font); self.font = None
        if self.renderer: self.destroy_renderer(self.renderer); self.renderer = None
        if self.window: self.destroy_window(self.window); self.window = None
        if self.ttf_initialized: self.ttf_quit(); self.ttf_initialized = False
        if self.initialized: self.quit(); self.initialized = False


def message(title, lines):
    screen = Screen()
    try:
        screen.draw(title, lines); screen.wait()
    finally:
        screen.close()
