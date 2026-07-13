workspace "Lemonade"
   configurations { "Debug", "Release" }
   architecture "x64"

outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

project "Engine"
   location "."
   kind "WindowedApp"
   dpiawareness "HighPerMonitor"
   language "C++"
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
      cppdialect "C++20"
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

   filter "system:linux"
      defines { "OS_LINUX", "OS=\"Linux\"" }

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
      cppdialect "C++20"
      buildoptions{"/utf-8"}
      defines { "OS_WINDOWS", "OS=\"Windows\"" }
      includedirs {
         "src",
         "modules/windows/catch2/include",
      }
      libdirs {
         "modules/windows/catch2/lib",
      }
      links { "catch2main", "catch2" }

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

      os.rmdir("./Lemonade.xcodeproj")
      os.rmdir("./Lemonade.xcworkspace")
      
      os.remove("*.sln")
      os.remove("*.vcxproj")
      os.remove("*.vcxproj.filters")
      os.remove("*.vcxproj.user")
      os.remove("Makefile")
      
      print("Clean complete!")
   end
}
