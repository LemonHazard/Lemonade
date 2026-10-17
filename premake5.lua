local unity = dofile("tools/unity.lua")

workspace "Lemonade"
   configurations { "Debug", "Release" }
   architecture "x64"

outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

project "Engine"
   location "."
   kind "WindowedApp"
   dpiawareness "HighPerMonitor"
   language "C++"
   cppdialect "C++20"
   objdir ("obj/" .. outputdir)
   targetdir ("build/" .. outputdir)

   files
   {
      "src/**.h",
      "src/**.hpp",
      "src/**.cpp"
   }

   enableunitybuild "On"

   filter "system:windows"
      buildoptions{"/utf-8"}
      defines { "OS_WINDOWS", "OS=\"Windows\"" }
      includedirs {
         "modules/windows/*/include",
      }
      libdirs {
         "modules/windows/*/lib",
         "$(DXSDK_DIR)/Lib/x64",
      }
      links { "d3d11", "d3dcompiler", "dxgi", "imgui", "assimp" }

   filter "system:macosx"
      defines { "OS_MACOS", "OS=\"MacOS\"" }
      xcodebuildsettings {
         ["GENERATE_INFOPLIST_FILE"] = "YES",
         ["MACOSX_DEPLOYMENT_TARGET"] = "12.0"
      }

   filter "system:linux"
      defines { "OS_LINUX", "OS=\"Linux\"" }
      includedirs {
         "modules/linux/*/include",
      }
      -- Link the runtime libraries by soname, so the -dev packages aren't needed
      links { ":libwayland-client.so.0", ":libdecor-0.so.0" }
      unity.build {
         replaces = { "src/**.cpp" },
         roots    = { "src/main.cpp" },
         out      = "build/app_unity.cpp",
      }

   filter "configurations:Debug"
      defines { "DEBUG", "_DEBUG" }
      symbols "On"

   filter "configurations:Release"
      defines { "NDEBUG", "_RELEASE", "RELEASE" }
      optimize "On"

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
      defines { "OS_WINDOWS", "OS=\"Windows\"" }
      includedirs {
         "src",
         "modules/common/catch2/include",
      }
      libdirs {
         "modules/windows/catch2/lib",
      }
      links { "catch2main", "catch2" }

   filter "system:macosx"
      defines { "OS_MACOS", "OS=\"MacOS\"" }
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
      defines { "OS_LINUX", "OS=\"Linux\"" }
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

newaction {
   trigger     = "clean",
   description = "Removes all generated project, solution, and binary files",
   execute = function ()
      print("Cleaning generated files...")
      
      os.rmdir("./obj")
      os.rmdir("./build")
      os.rmdir("./.vs")

      os.rmdir("./Engine.xcodeproj")
      os.rmdir("./Engine_Spec.xcodeproj")
      os.rmdir("./Lemonade.xcworkspace")
      
      os.remove("*.sln")
      os.remove("*.vcxproj")
      os.remove("*.vcxproj.filters")
      os.remove("*.vcxproj.user")
      os.remove("Makefile")
      os.remove("*.make")
      
      print("Clean complete!")
   end
}
