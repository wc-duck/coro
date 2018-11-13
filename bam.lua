--[[
   Header implementing "protothreads" but with a stack to support
   local-varible state, argument-passing and sub-coroutines.

   version 1.0, november, 2018

   Copyright (C) 2018- Fredrik Kihlander

   https://github.com/wc-duck/coro

   This software is provided 'as-is', without any express or implied
   warranty.  In no event will the authors be held liable for any damages
   arising from the use of this software.

   Permission is granted to anyone to use this software for any purpose,
   including commercial applications, and to alter it and redistribute it
   freely, subject to the following restrictions:

   1. The origin of this software must not be misrepresented; you must not
      claim that you wrote the original software. If you use this software
      in a product, an acknowledgment in the product documentation would be
      appreciated but is not required.
   2. Altered source versions must be plainly marked as such, and must not be
      misrepresented as being the original software.
   3. This notice may not be removed or altered from any source distribution.

   Fredrik Kihlander
--]]

BUILD_PATH = "local"

function get_config()
    local config = ScriptArgs["config"]
    if config == nil then
        return "debug"
    end
    return config
end

function get_platform()
    local platform = ScriptArgs["platform"]
    if platform == nil then
        if family == "windows" then
            platform = "winx64"
        else
            platform = "linux_x86_64"
        end
    end
    return platform
end

function get_compiler()
    if family == "windows" then
        return "msvc"
    end

    local compiler = ScriptArgs["compiler"]
    if compiler ~= nil then
        return compiler
    end

    return "gcc"
end

function get_base_settings()
    local settings = {}

    settings._is_settingsobject = true
    settings.invoke_count = 0
    settings.debug = 0
    settings.optimize = 0
    SetCommonSettings(settings)

    -- add all tools
    for _, tool in pairs(_bam_tools) do
        tool(settings)
    end

    return settings
end

function set_compiler( settings, config, compiler )
    InitCommonCCompiler(settings)
    if compiler == "msvc" then
        SetDriversCL( settings )
        if config == "release" then
            settings.cc.flags:Add( "/Ox" )
            settings.cc.flags:Add( "/TP" ) -- forcing c++ compile on windows =/
        end
    end

    if compiler == "gcc" or compiler == "clang" then
        if compiler == "gcc" then
            SetDriversGCC( settings )
        else
            SetDriversClang( settings )
        end    
        settings.cc.flags:Add( "-Wconversion", "-Wextra", "-Wall", "-Werror", "-Wstrict-aliasing=2" )
        if config == "release" then
            settings.cc.flags:Add( "-O2" )
        end
    end
end

config   = get_config()
platform = get_platform()
compiler = get_compiler()
settings = get_base_settings()
set_compiler( settings, config, compiler )
TableLock( settings )

local output_path = PathJoin( BUILD_PATH, PathJoin( PathJoin( config, platform ), compiler ) )
local output_func = function(settings, path) return PathJoin(output_path, PathFilename(PathBase(path)) .. settings.config_ext) end
settings.cc.Output = output_func
settings.lib.Output = output_func
settings.link.Output = output_func

settings.link.libpath:Add( 'local/' .. config .. '/' .. platform )
local tests = Link( settings, 'coro_tests', Compile( settings, 'test/test_coro.cpp' ) )

test_args = " -v"
if ScriptArgs["test"]     then test_args = test_args .. " -t " .. ScriptArgs["test"] end
if ScriptArgs["suite"]    then test_args = test_args .. " -s " .. ScriptArgs["suite"] end

if family == "windows" then
	AddJob( "test",     "unittest",  string.gsub( tests, "/", "\\" ) .. test_args, tests, tests )
else
	AddJob( "test",     "unittest",  tests .. test_args, tests, tests )
	AddJob( "valgrind", "valgrind",  "valgrind -v --leak-check=full --track-origins=yes " .. tests .. test_args, tests, tests )
end

PseudoTarget( "all", tests )
DefaultTarget( "all" )

