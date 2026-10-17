-- Unity build gatherer.
--
-- Starting from root source files, follows project includes and writes one .cpp file
-- that #includes every source the roots depend on:
--   - only #include "..." is followed; #include <...> is system or third-party code;
--   - a quoted include is resolved next to the including file, then in include dirs,
--     and skipped if not found there (e.g. third-party headers from modules/);
--   - for every visited file dir/name.ext the companion sources are pulled in:
--     dir/name.cpp (for headers) and dir/<os>/name.cpp (for any file), so
--     window.h brings in linux/window.cpp and main.cpp brings in linux/main.cpp;
--   - #if/#ifdef are not evaluated, so platform files must stay wrapped in #ifdef OS_*.
--
-- Sources are emitted after the sources they depend on, and the output is only
-- rewritten when its content changes, so an unchanged unity file isn't recompiled.
--
-- Usage from premake5.lua (inside a project, Linux only for now):
--   unity.build {
--       replaces     = { "src/**.cpp" },        -- project files no longer compiled directly
--       roots        = { "src/main.cpp" },
--       include_dirs = { "src" },               -- optional
--       out          = "build/app_unity.cpp",
--   }
--
-- Standalone:
--   premake5 --file=tools/unity.lua --os=linux unity --roots=src/main.cpp --out=build/app_unity.cpp

local unity = {}

-- This script's own path, for the prebuild command: in a game the engine (and this
-- script) usually lives in a subfolder, not next to the main premake script.
local UNITY_SCRIPT = _SCRIPT

-- Unity files generated in this run: absolute output path -> gathered sources.
unity.generated = {}

local os_dirs = { windows = "windows", linux = "linux", macosx = "macos" }

local function parse_includes(source)
    source = source:gsub("/%*.-%*/", "")

    local includes = {}
    for line in source:gmatch("[^\n]+") do
        local name = line:match('^%s*#%s*include%s*"([^"]+)"')
        if name then
            table.insert(includes, name)
        end
    end
    return includes
end

local function resolve(name, from_dir, include_dirs)
    local candidate = path.join(from_dir, name)
    if os.isfile(candidate) then
        return path.getabsolute(candidate)
    end

    for _, dir in ipairs(include_dirs) do
        candidate = path.join(dir, name)
        if os.isfile(candidate) then
            return path.getabsolute(candidate)
        end
    end

    return nil
end

local function companions(file, os_dir)
    local dir  = path.getdirectory(file)
    local name = path.getbasename(file)
    local ext  = path.getextension(file)

    local list = {}
    if ext == ".h" or ext == ".hpp" then
        table.insert(list, path.join(dir, name .. ".cpp"))
    end
    table.insert(list, path.join(dir, os_dir, name .. ".cpp"))
    return list
end

local function expand_roots(roots, base_dir)
    local files = {}
    for _, root in ipairs(roots) do
        local pattern = path.getabsolute(root, base_dir)
        if pattern:find("*", 1, true) then
            local matches = os.matchfiles(pattern)
            table.sort(matches)
            for _, file in ipairs(matches) do
                table.insert(files, path.getabsolute(file))
            end
        elseif os.isfile(pattern) then
            table.insert(files, pattern)
        else
            error("unity: root file not found: " .. root, 0)
        end
    end
    return files
end

-- Returns the list of gathered sources (absolute paths) in emission order.
function unity.gather(roots, include_dirs, base_dir)
    base_dir = base_dir or os.getcwd()

    local os_dir = os_dirs[os.target()]
    if not os_dir then
        error("unity: unsupported target os: " .. tostring(os.target()), 0)
    end

    local dirs = {}
    for _, dir in ipairs(include_dirs or {}) do
        table.insert(dirs, path.getabsolute(dir, base_dir))
    end

    local visited = {}
    local sources = {}

    local function visit(file)
        if visited[file] then return end
        visited[file] = true

        local source = io.readfile(file)
        if not source then
            error("unity: could not read " .. file, 0)
        end

        for _, name in ipairs(parse_includes(source)) do
            local include = resolve(name, path.getdirectory(file), dirs)
            if include then
                visit(include)
            end
        end

        for _, companion in ipairs(companions(file, os_dir)) do
            if os.isfile(companion) then
                visit(path.getabsolute(companion))
            end
        end

        if path.getextension(file) == ".cpp" then
            table.insert(sources, file)
        end
    end

    for _, root in ipairs(expand_roots(roots, base_dir)) do
        visit(root)
    end

    return sources
end

-- Writes the unity file; returns true if it was (re)written.
function unity.generate(roots, include_dirs, out, base_dir)
    base_dir = base_dir or os.getcwd()

    local out_file = path.getabsolute(out, base_dir)
    local out_dir  = path.getdirectory(out_file)
    local sources  = unity.gather(roots, include_dirs, base_dir)

    local lines = { "// Generated by tools/unity.lua, do not edit.", "" }
    for _, source in ipairs(sources) do
        table.insert(lines, '#include "' .. path.getrelative(out_dir, source) .. '"')
    end
    local content = table.concat(lines, "\n") .. "\n"

    unity.generated[out_file] = sources

    os.mkdir(out_dir)
    local result = os.writefile_ifnotequal(content, out_file)
    if result < 0 then
        error("unity: could not write " .. out_file, 0)
    end
    if result > 0 then
        print("unity: generated " .. path.getrelative(base_dir, out_file) .. " (" .. #sources .. " sources)")
    end
    return result > 0
end

-- Sets up the current project to build a single unity file: the files matching
-- opts.replaces are removed from the build, the unity file is generated now (so it
-- exists for make) and regenerated by a prebuild command, so new includes are picked
-- up without re-running premake. Paths are relative to the main premake script.
function unity.build(opts)
    local base_dir     = _MAIN_SCRIPT_DIR
    local include_dirs = opts.include_dirs or {}

    -- premake applies removals after all additions, so the patterns
    -- must not match opts.out
    removefiles(opts.replaces)
    files { opts.out }

    local function quote(s) return '"' .. s .. '"' end
    local function absolute(list)
        local result = {}
        for _, p in ipairs(list) do
            table.insert(result, path.getabsolute(p, base_dir))
        end
        return table.concat(result, ",")
    end

    local command = "@" .. quote(_PREMAKE_COMMAND)
        .. " --file=" .. quote(UNITY_SCRIPT)
        .. " --os=" .. os.target()
        .. " unity"
        .. " --roots=" .. quote(absolute(opts.roots))
        .. " --out=" .. quote(path.getabsolute(opts.out, base_dir))
    if #include_dirs > 0 then
        command = command .. " --include-dirs=" .. quote(absolute(include_dirs))
    end
    prebuildcommands { command }

    if os.istarget("linux") and (_ACTION == "gmake" or _ACTION == "gmake2") then
        unity.generate(opts.roots, include_dirs, opts.out, base_dir)
    end
end

-- Editors (VSCode's C/C++ extension, clangd) need compile flags for every source,
-- but the compiler only ever sees the unity file. Write a compile_commands.json with
-- one entry per gathered source, using the flags of the project's Debug config.
function unity.write_compile_commands(file_path)
    local gcc = premake.tools.gcc

    local entries = {}
    for wks in premake.global.eachWorkspace() do
        for prj in premake.workspace.eachproject(wks) do
            local cfg = premake.project.getconfig(prj, "Debug")
            for _, file in ipairs(cfg and cfg.files or {}) do
                local sources = unity.generated[file]
                if sources then
                    local flags = table.join({ gcc.gettoolname(cfg, "cxx") },
                                             gcc.getcxxflags(cfg),
                                             gcc.getdefines(cfg.defines, cfg),
                                             gcc.getincludedirs(cfg, cfg.includedirs, cfg.externalincludedirs,
                                                                cfg.frameworkdirs, cfg.includedirsafter),
                                             gcc.getforceincludes(cfg),
                                             cfg.buildoptions)
                    local command = table.concat(flags, " ")

                    for _, source in ipairs(sources) do
                        table.insert(entries, {
                            directory = prj.location,
                            file      = source,
                            command   = command .. " -c " .. path.getrelative(prj.location, source),
                        })
                    end
                end
            end
        end
    end

    if #entries == 0 then return end

    local function quote(s)
        return '"' .. s:gsub('\\', '\\\\'):gsub('"', '\\"') .. '"'
    end

    local lines = {}
    for i, entry in ipairs(entries) do
        table.insert(lines, "  {\n"
            .. '    "directory": ' .. quote(entry.directory) .. ",\n"
            .. '    "file": '      .. quote(entry.file)      .. ",\n"
            .. '    "command": '   .. quote(entry.command)   .. "\n"
            .. "  }" .. (i < #entries and "," or ""))
    end
    local content = "[\n" .. table.concat(lines, "\n") .. "\n]\n"

    if os.writefile_ifnotequal(content, file_path) > 0 then
        print("unity: generated " .. path.getrelative(_MAIN_SCRIPT_DIR, file_path) .. " (" .. #entries .. " entries)")
    end
end

premake.override(premake.main, "postAction", function(base)
    if os.istarget("linux") and (_ACTION == "gmake" or _ACTION == "gmake2") then
        unity.write_compile_commands(path.join(_MAIN_SCRIPT_DIR, "compile_commands.json"))
    end
    base()
end)

-- Sources may #include files produced by custom build commands (e.g. SPIR-V
-- compiled from shaders). premake's makefiles only order those before linking, so
-- `make -j` could compile the unity file first; make objects wait for them too.
if premake.modules.gmake and premake.modules.gmake.cpp then
    premake.override(premake.modules.gmake.cpp, "customDeps", function(base, cfg, toolset)
        base(cfg, toolset)
        if cfg._gmake.filesets["CUSTOM"] then
            _p('$(OBJECTS): | $(CUSTOM)')
        end
    end)
end

newoption {
    trigger     = "roots",
    value       = "FILES",
    description = "unity: comma-separated root source files (globs allowed)",
}

newoption {
    trigger     = "include-dirs",
    value       = "DIRS",
    description = "unity: comma-separated include dirs to resolve quoted includes",
}

newoption {
    trigger     = "out",
    value       = "FILE",
    description = "unity: output unity source file",
}

newaction {
    trigger     = "unity",
    description = "Generate a unity build source file from the given roots",
    execute     = function()
        if not _OPTIONS["roots"] or not _OPTIONS["out"] then
            error("unity: --roots and --out are required", 0)
        end

        local function split(s)
            return s and s ~= "" and string.explode(s, ",") or {}
        end

        unity.generate(split(_OPTIONS["roots"]),
                       split(_OPTIONS["include-dirs"]),
                       _OPTIONS["out"],
                       _WORKING_DIR)
    end,
}

return unity
