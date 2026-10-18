local lemonade = dofile("lemonade.lua")
local unity    = lemonade.unity

workspace "Lemonade"
   configurations { "Debug", "Release" }
   architecture "x64"
   startproject "Engine"

lemonade.engine { name = "Engine", game = "Sandbox", title = "Lemonade" }
lemonade.game   { name = "Sandbox", dir = "sandbox", roots = { "sandbox/sandbox.cpp" } }

project "Engine_Spec"
   location "."
   kind "ConsoleApp"
   dpiawareness "HighPerMonitor"
   language "C++"
   cppdialect "C++20"
   objdir ("obj/" .. outputdir)
   targetdir ("build/" .. outputdir)
   debugdir "."

   files
   {
      ".runsettings",
      "test/**.h",
      "test/**.hpp",
      "test/**.cpp"
   }

   enableunitybuild "On"

   filter "system:windows"
      buildoptions{"/utf-8"}
      defines { "OS_WINDOWS", "OS_NAME=\"Windows\"" }
      includedirs {
         "src",
         "modules/common/catch2/include",
      }
      libdirs {
         "modules/windows/catch2/lib",
      }
      links { "catch2main", "catch2" }

   filter "system:macosx"
      defines { "OS_MACOS", "OS_NAME=\"MacOS\"" }
      xcodebuildsettings {
         ["GENERATE_INFOPLIST_FILE"] = "YES",
         ["MACOSX_DEPLOYMENT_TARGET"] = "12.0"
      }
      xcodebuildsettings {
        ["HEADER_SEARCH_PATHS"] = {
            "$(inherited)",
            "src",
            "modules/common/catch2/include",
        }
      }
      -- includedirs {
      --    "src",
      --    "modules/common/catch2/include",
      -- }
      libdirs {
         "modules/macos/catch2/lib",
      }
      links { "catch2main", "catch2" }

   filter "system:linux"
      defines { "OS_LINUX", "OS_NAME=\"Linux\"" }
      includedirs {
         "src",
         "modules/common/catch2/include",
      }
      libdirs {
         "modules/linux/catch2/lib",
      }
      links { "catch2main", "catch2" }
      unity.build {
         replaces     = { "test/**.cpp" },
         roots        = { "test/**.cpp" },
         include_dirs = { "src" },
         out          = "build/spec_unity.cpp",
      }

   filter "configurations:Debug"
      defines { "DEBUG" }
      runtime "Release"
      editandcontinue "Off"
      symbols "Full"

   filter "configurations:Release"
      defines { "NDEBUG", "_RELEASE", "RELEASE" }
      optimize "On"
