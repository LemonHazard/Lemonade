-- Build setup for the engine and the games made with it.
--
-- A game is a shared library the engine executable loads, and reloads whenever it's
-- rebuilt, so code changes show up in the running game (see src/game.h). The engine's
-- own premake5.lua builds it with the sandbox game; a game keeps the engine in a
-- subfolder and its premake5.lua does:
--
--   local lemonade = dofile("lemonade/lemonade.lua")
--
--   workspace "My_Game"
--      configurations { "Debug", "Release" }
--      architecture "x64"
--      startproject "My_Game"
--
--   lemonade.engine { name = "My_Game", game = "My_Game_Code", title = "My Game" }
--   lemonade.game   { name = "My_Game_Code", dir = "src", roots = { "src/my_game.cpp" } }
--
-- Relative paths are relative to the main premake script: both projects build into
-- its build/<config>-<os>-<arch>/, as the engine looks for the game library next to
-- its executable.

local ENGINE_DIR = _SCRIPT_DIR

local unity = dofile(path.join(ENGINE_DIR, "tools/unity.lua"))
dofile(path.join(ENGINE_DIR, "tools/vscode.lua"))

local lemonade = { unity = unity }

local function engine_path(p)
   return path.join(ENGINE_DIR, p)
end

-- Rendering backend, chosen at build time: each one lives in src/<renderer>/ and is
-- compiled in with RENDERER_<NAME> defined.
newoption {
   trigger     = "renderer",
   value       = "API",
   description = "Rendering backend (default: vulkan on Linux, null elsewhere)",
   allowed     = {
      { "vulkan", "Vulkan (Linux)" },
      { "null",   "Draws nothing" },
   },
}

local renderer = _OPTIONS["renderer"] or (os.istarget("linux") and "vulkan" or "null")
if renderer == "vulkan" and not os.istarget("linux") then
   error("The Vulkan renderer is only supported on Linux for now", 0)
end
lemonade.renderer = renderer

outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

-- Engine sources the game compiles in. On Linux the unity build finds them by
-- following the game's includes.
local GAME_ENGINE_SOURCES = {
   engine_path("src/file.cpp"),
   engine_path("src/input.cpp"),
   engine_path("src/lighting.cpp"),
   engine_path("src/level.cpp"),
   engine_path("src/editor.cpp"),
   engine_path("src/world.cpp"),
   engine_path("src/world_game.cpp"),
   engine_path("src/json.cpp"),
   engine_path("src/model.cpp"),
   engine_path("src/palette.cpp"),
   engine_path("src/gltf.cpp"),
   engine_path("src/png.cpp"),
}

local IMGUI_DIR = engine_path("modules/common/imgui")

-- Settings the engine and the game share.
local function defaults()
   location "."
   language "C++"
   cppdialect "C++20"
   objdir ("obj/" .. outputdir)
   targetdir ("build/" .. outputdir)

   filter "system:windows"
      buildoptions { "/utf-8" }
      defines { "OS_WINDOWS", "OS_NAME=\"Windows\"" }

   filter "system:macosx"
      defines { "OS_MACOS", "OS_NAME=\"MacOS\"" }
      xcodebuildsettings {
         ["GENERATE_INFOPLIST_FILE"] = "YES",
         ["MACOSX_DEPLOYMENT_TARGET"] = "12.0"
      }

   filter "system:linux"
      defines { "OS_LINUX", "OS_NAME=\"Linux\"" }

   filter "configurations:Debug"
      defines { "DEBUG", "_DEBUG" }
      symbols "On"

   filter "configurations:Release"
      defines { "NDEBUG", "_RELEASE", "RELEASE" }
      optimize "On"

   filter {}
end

-- Dear ImGui, for the game's tools (see src/imgui_host.h). The engine and the game both
-- link it, so it's a library of its own: built once, not again with every rebuild of
-- the game. Made with the workspace's first engine or game project.
local imgui_made = false
local function imgui_library()
   if imgui_made then return end
   imgui_made = true

   project "ImGui"
      kind "StaticLib"
      defaults()
      -- Linked into the game library too, where it stays out of the exported symbols.
      pic "On"
      visibility "Hidden"

      files {
         path.join(IMGUI_DIR, "include/*.h"),
         path.join(IMGUI_DIR, "src/*.cpp"),
      }
      includedirs { path.join(IMGUI_DIR, "include") }

      -- As for the game library, which this goes into (see lemonade.game).
      filter { "system:linux", "toolset:gcc" }
         buildoptions { "-fno-gnu-unique" }

      filter {}
end

-- The engine executable. opts:
--   name   project and executable name
--   game   the game library's project name (see lemonade.game)
--   title  window title (default: name)
function lemonade.engine(opts)
   imgui_library()

   project(opts.name)
      kind "WindowedApp"
      dpiawareness "HighPerMonitor"
      defaults()
      debugdir "."
      dependson { opts.game }

      files {
         engine_path("src/**.h"),
         engine_path("src/**.hpp"),
         engine_path("src/**.cpp"),
      }

      -- png.cpp decodes with stb_image
      includedirs { engine_path("modules/common/stb/include"), path.join(IMGUI_DIR, "include") }
      links { "ImGui" }

      defines {
         "RENDERER_" .. renderer:upper(),
         "GAME_LIBRARY=\"" .. opts.game .. "\"",
         "GAME_TITLE=\"" .. (opts.title or opts.name) .. "\"",
      }

      enableunitybuild "On"

      filter "system:windows"
         includedirs { engine_path("modules/windows/*/include") }
         libdirs {
            engine_path("modules/windows/*/lib"),
            "$(DXSDK_DIR)/Lib/x64",
         }
         links { "d3d11", "d3dcompiler", "dxgi" }

      filter "system:linux"
         includedirs { engine_path("modules/linux/*/include") }
         -- Link the runtime libraries by soname, so the -dev packages aren't needed
         links { ":libwayland-client.so.0", ":libwayland-cursor.so.0", ":libdecor-0.so.0", ":libxkbcommon.so.0", "dl" }
         unity.build {
            replaces      = { engine_path("src/**.cpp") },
            roots         = { engine_path("src/main.cpp") },
            -- modules whose sources are compiled into the unity build (include/ + src/)
            include_dirs  = { engine_path("modules/linux/*/include") },
            -- companion sources come from src/linux/ and the renderer's src/<renderer>/
            platform_dirs = { "linux", renderer },
            out           = "build/" .. opts.name .. "_unity.cpp",
         }

      if renderer == "vulkan" then
         filter "system:linux"
            links { ":libvulkan.so.1" }
            -- GLSL shaders are compiled to SPIR-V by glslc and included as C arrays
            files { engine_path("src/vulkan/shaders/*.vert"), engine_path("src/vulkan/shaders/*.frag") }
            includedirs { engine_path("modules/common/vulkan/include"), "build/shaders" }

         -- file filters only match paths relative to the calling script
         filter { "system:linux", "files:" .. path.getrelative(os.getcwd(), engine_path("src/vulkan/shaders/*")) }
            buildmessage "%{file.name}"
            buildcommands {
               '{MKDIR} "build/shaders"',
               'glslc --target-env=vulkan1.3 -mfmt=c -o "build/shaders/%{file.name}.inc" "%{file.relpath}"',
            }
            buildoutputs { "build/shaders/%{file.name}.inc" }
      end

      filter {}
end

-- The game library. opts:
--   name   project and library name
--   dir    the game's sources
--   roots  the sources the unity build starts from (globs allowed); the others are
--          found by following their includes
function lemonade.game(opts)
   imgui_library()

   project(opts.name)
      kind "SharedLib"
      defaults()
      -- Only what's marked GAME_EXPORT
      visibility "Hidden"

      files {
         path.join(opts.dir, "**.h"),
         path.join(opts.dir, "**.hpp"),
         path.join(opts.dir, "**.cpp"),
      }

      includedirs { opts.dir, engine_path("src"), engine_path("modules/common/stb/include"), path.join(IMGUI_DIR, "include") }
      links { "ImGui" }

      -- The engine doesn't load the library while the lock file exists: the linker
      -- may not be done writing it.
      prelinkcommands   { '@{TOUCH} "%{cfg.buildtarget.abspath}.lock"' }
      postbuildcommands { '@{DELETE} "%{cfg.buildtarget.abspath}.lock"' }

      enableunitybuild "On"

      filter "system:not linux"
         files (GAME_ENGINE_SOURCES)

      filter { "system:linux", "toolset:gcc" }
         -- dlclose() can't unload a library with GNU unique symbols (statics of inline
         -- functions and templates), so reloads would keep running the old code.
         buildoptions { "-fno-gnu-unique" }

      filter "system:linux"
         unity.build {
            replaces     = { path.join(opts.dir, "**.cpp") },
            roots        = opts.roots,
            include_dirs = { opts.dir, engine_path("src") },
            out          = "build/" .. opts.name .. "_unity.cpp",
         }

      filter {}
end

newaction {
   trigger     = "clean",
   description = "Removes all generated project, solution, and binary files",
   execute = function ()
      print("Cleaning generated files...")

      local function at_root(p)
         return path.join(_MAIN_SCRIPT_DIR, p)
      end

      for _, dir in ipairs { "obj", "build", ".vs" } do
         os.rmdir(at_root(dir))
      end
      for _, pattern in ipairs { "*.xcodeproj", "*.xcworkspace" } do
         for _, dir in ipairs(os.matchdirs(at_root(pattern))) do
            os.rmdir(dir)
         end
      end
      for _, pattern in ipairs {
         "*.sln", "*.vcxproj", "*.vcxproj.filters", "*.vcxproj.user",
         "Makefile", "*.make", "compile_commands.json",
         ".vscode/tasks.json", ".vscode/launch.json",
      } do
         os.remove(at_root(pattern))
      end

      print("Clean complete!")
   end
}

return lemonade
