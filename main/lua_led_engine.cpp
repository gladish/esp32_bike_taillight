/**
 * lua_led_engine.cpp
 *
 * Sandboxed Lua 5.4 engine for LED animation scripts. This is the
 * host-testable version: same API and sandboxing as what will become
 * LuaScriptRenderer on the microcontroller, but built against system
 * Lua so it can be iterated on without touching hardware.
 */

#include "lua_led_engine.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include <array>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <string_view>


// ---------------------------------------------------------------------------
// Internal structure
// ---------------------------------------------------------------------------

struct LuaLedEngine {
  lua_State*   L;
  char         last_error[256];
  int          led_count;
  int          strip_ref;                     // registry ref to the reusable `strip` table
  LuaLedResult scratch[LUA_LED_MAX_COUNT];     // written by strip.set(), committed on success
};

// ---------------------------------------------------------------------------
// Sandbox: only these libs are opened
// ---------------------------------------------------------------------------

constexpr std::array<luaL_Reg, 4> SAFE_LIBS = {{
    {"_G",            luaopen_base},
    {LUA_MATHLIBNAME, luaopen_math},
    {LUA_TABLIBNAME,  luaopen_table},
    {LUA_STRLIBNAME,  luaopen_string},
}};

// Remove dangerous functions from the base library that luaopen_base adds.
static void sandbox_base(lua_State* L) {
  constexpr std::string_view globals[] = {
    "dofile",
    "loadfile",
    "require",
    "collectgarbage",
    "rawget",
    "rawset",
    "rawequal",
    "rawlen",
    "load"
  };

  for (const auto name : globals) {
    lua_pushnil(L);
    lua_setglobal(L, name.data());
  }
}

// ---------------------------------------------------------------------------
// strip.set(i, r, g, b [, brightness])
// ---------------------------------------------------------------------------

static int l_strip_set(lua_State* L) {
  LuaLedEngine* engine = (LuaLedEngine*)lua_touserdata(L, lua_upvalueindex(1));

  lua_Integer i          = luaL_checkinteger(L, 1);
  lua_Integer r          = luaL_checkinteger(L, 2);
  lua_Integer g          = luaL_checkinteger(L, 3);
  lua_Integer b          = luaL_checkinteger(L, 4);
  lua_Integer brightness = luaL_optinteger(L, 5, 16);

  if (i < 0 || i >= engine->led_count) {
    return luaL_error(L, "strip.set: index %d out of range (0..%d)",
                       (int) i, engine->led_count - 1);
  }
  if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) {
    return luaL_error(L, "strip.set: r/g/b must be 0-255 (got %d,%d,%d)",
                       (int)r, (int)g, (int)b);
  }
  if (brightness < 0 || brightness > LUA_LED_MAX_BRIGHTNESS) {
    return luaL_error(L, "strip.set: brightness must be 0-%d (got %d)",
                       LUA_LED_MAX_BRIGHTNESS, (int)brightness);
  }

  engine->scratch[i].r          = (uint8_t)r;
  engine->scratch[i].g          = (uint8_t)g;
  engine->scratch[i].b          = (uint8_t)b;
  engine->scratch[i].brightness = (uint8_t)brightness;
  return 0;
}

// Builds a fresh `strip` table bound to this engine and leaves it on top
// of the stack. Called once per load(), not once per tick() -- the whole
// point is that a script's per-frame work is C calls into a fixed buffer,
// not fresh Lua table construction.
static void push_strip_table(lua_State* L, LuaLedEngine* engine) {
  lua_newtable(L);
  lua_pushlightuserdata(L, engine);
  lua_pushcclosure(L, l_strip_set, 1);
  lua_setfield(L, -2, "set");
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

LuaLedEngine* lua_led_engine_create(void) {
  LuaLedEngine* engine = (LuaLedEngine*)calloc(1, sizeof(LuaLedEngine));
  if (!engine) return NULL;

  engine->strip_ref = LUA_NOREF;

  engine->L = luaL_newstate();
  if (!engine->L) {
    free(engine);
    return NULL;
  }

  for (const auto& lib : SAFE_LIBS) {
    luaL_requiref(engine->L, lib.name, lib.func, 1);
    lua_pop(engine->L, 1);
  }
  sandbox_base(engine->L);

  snprintf(engine->last_error, sizeof(engine->last_error), "no error");
  return engine;
}

void lua_led_engine_destroy(LuaLedEngine* engine) {
  if (!engine) return;
  if (engine->L) lua_close(engine->L);  // also frees the strip_ref'd table
  free(engine);
}

bool lua_led_engine_load(LuaLedEngine* engine, const char* source, int led_count) {
  if (!engine || !source) return false;

  if (led_count <= 0 || led_count > LUA_LED_MAX_COUNT) {
    snprintf(engine->last_error, sizeof(engine->last_error),
             "led_count %d out of range (1..%d)", led_count, LUA_LED_MAX_COUNT);
    return false;
  }
  if (strlen(source) > LUA_SCRIPT_MAX_SIZE) {
    snprintf(engine->last_error, sizeof(engine->last_error),
             "script too large: %zu bytes (max %d)", strlen(source), LUA_SCRIPT_MAX_SIZE);
    return false;
  }

  engine->led_count = led_count;
  memset(engine->scratch, 0, sizeof(engine->scratch));

  lua_State* L = engine->L;

  int rc = luaL_loadstring(L, source);
  if (rc != LUA_OK) {
    snprintf(engine->last_error, sizeof(engine->last_error), "compile: %s", lua_tostring(L, -1));
    lua_pop(L, 1);
    return false;
  }

  rc = lua_pcall(L, 0, 0, 0);
  if (rc != LUA_OK) {
    snprintf(engine->last_error, sizeof(engine->last_error), "init: %s", lua_tostring(L, -1));
    lua_pop(L, 1);
    return false;
  }

  lua_getglobal(L, "LedStrip_Tick");
  bool has_render = lua_isfunction(L, -1);
  lua_pop(L, 1);
  if (!has_render) {
    snprintf(engine->last_error, sizeof(engine->last_error),
             "script must define a global function LedStrip_Tick(strip, now_ms)");
    return false;
  }

  if (engine->strip_ref != LUA_NOREF) {
    luaL_unref(L, LUA_REGISTRYINDEX, engine->strip_ref);
  }
  push_strip_table(L, engine);
  engine->strip_ref = luaL_ref(L, LUA_REGISTRYINDEX);

  // Optional one-time setup: LedStrip_Setup(led_count).
  lua_getglobal(L, "LedStrip_Setup");
  if (lua_isfunction(L, -1)) {
    lua_pushinteger(L, led_count);
    rc = lua_pcall(L, 1, 0, 0);
    if (rc != LUA_OK) {
      snprintf(engine->last_error, sizeof(engine->last_error), "setup: %s", lua_tostring(L, -1));
      lua_pop(L, 1);
      return false;
    }
  } else {
    lua_pop(L, 1);
  }

  snprintf(engine->last_error, sizeof(engine->last_error), "ok");
  return true;
}

bool lua_led_engine_tick(LuaLedEngine* engine, double now_ms, int led_count, LuaLedResult* out_leds) {
  if (!engine || !out_leds) return false;
  if (led_count != engine->led_count) {
    snprintf(engine->last_error, sizeof(engine->last_error),
             "tick led_count (%d) doesn't match loaded led_count (%d)",
             led_count, engine->led_count);
    return false;
  }

  lua_State* L = engine->L;

  lua_getglobal(L, "LedStrip_Tick");
  lua_rawgeti(L, LUA_REGISTRYINDEX, engine->strip_ref);
  lua_pushnumber(L, now_ms);

  int rc = lua_pcall(L, 2, 0, 0);
  if (rc != LUA_OK) {
    snprintf(engine->last_error, sizeof(engine->last_error), "render: %s", lua_tostring(L, -1));
    lua_pop(L, 1);
    return false;  // out_leds left untouched -- caller keeps last good frame
  }

  memcpy(out_leds, engine->scratch, sizeof(LuaLedResult) * (size_t)led_count);
  return true;
}

const char* lua_led_engine_last_error(const LuaLedEngine* engine) {
  if (!engine) return "null engine";
  return engine->last_error;
}

size_t lua_led_engine_heap_used(const LuaLedEngine* engine) {
  if (!engine || !engine->L) return 0;
  return (size_t)lua_gc(engine->L, LUA_GCCOUNT, 0) * 1024
       + (size_t)lua_gc(engine->L, LUA_GCCOUNTB, 0);
}
