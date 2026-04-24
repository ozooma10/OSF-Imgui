-- include subprojects
includes("lib/commonlibsf")

add_requires("directxtk12", {
    configs = {
        testing = false,
        gameinput = false,
        xinput = false,
        wgi = false,
        xaudio_redist = false
    }
})
add_requires("nanosvg")

-- set project constants
set_project("SFSEMenuFramework")
set_version("0.0.1")
set_license("MIT")
set_languages("c++23")
set_warnings("allextra")

-- add common rules
add_rules("mode.debug", "mode.releasedbg")
add_rules("plugin.vsxmake.autoupdate")

-- define targets
target("SFSEMenuFramework")
    add_rules("commonlibsf.plugin", {
        name = "SFSEMenuFramework",
        author = "libxse",
        description = "SFSE Menu Framework",
        email = "getlit@biz.biz"
    })

    -- add src files
    add_files("src/**.cpp")
    add_files("lib/cimgui/imgui/imgui.cpp")
    add_files("lib/cimgui/imgui/imgui_draw.cpp")
    add_files("lib/cimgui/imgui/imgui_tables.cpp")
    add_files("lib/cimgui/imgui/imgui_widgets.cpp")
    add_files("lib/cimgui/imgui/imgui_demo.cpp")
    add_files("lib/cimgui/imgui/backends/imgui_impl_dx12.cpp")
    add_files("lib/cimgui/imgui/backends/imgui_impl_win32.cpp")
    add_files("lib/cimgui/cimgui.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    add_includedirs("lib/cimgui", "lib/cimgui/imgui", "lib/cimgui/imgui/backends")
    add_packages("directxtk12", "nanosvg")
    set_pcxxheader("src/pch.h")
    add_defines("NOMINMAX", "WIN32_LEAN_AND_MEAN", "IMGUI_IMPL_WIN32_DISABLE_GAMEPAD", "IMGUI_DEFINE_MATH_OPERATORS")
    add_syslinks("d3d12", "dxgi", "user32")

    -- add resource files (copied alongside the DLL on `xmake install`)
    add_installfiles("resources/fonts/*.ttf",  { prefixdir = "SFSE/Plugins/Fonts" })
    add_installfiles("resources/cursor/*.png", { prefixdir = "SFSE/Plugins/Cursor" })

    after_install(function(target)
        local _, dstfiles = target:installfiles()
        if not dstfiles or #dstfiles == 0 then
            return
        end

        cprint("${bright green}[SFSEMenuFramework] install copy results:")
        for _, dstfile in ipairs(dstfiles) do
            if os.isfile(dstfile) then
                cprint("${green}  copied:${clear} %s", dstfile)
            else
                cprint("${red}  missing:${clear} %s", dstfile)
            end
        end
    end)
