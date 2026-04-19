-- include subprojects
includes("lib/commonlibsf")

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
    add_files("lib/imgui/*.cpp")
    add_files("lib/imgui/backends/imgui_impl_dx12.cpp")
    add_files("lib/imgui/backends/imgui_impl_win32.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    add_includedirs("lib/imgui", "lib/imgui/backends")
    set_pcxxheader("src/pch.h")
    add_defines("NOMINMAX", "WIN32_LEAN_AND_MEAN", "IMGUI_IMPL_WIN32_DISABLE_GAMEPAD")
    add_syslinks("d3d12", "dxgi", "user32")

    after_build(function(target)
        local modsroot = os.getenv("XSE_SF_MODS_PATH")
        local gameroot = os.getenv("XSE_SF_GAME_PATH")
        local plugindir = nil

        if modsroot and #modsroot > 0 then
            plugindir = path.join(modsroot, target:name(), "SFSE", "Plugins")
        elseif gameroot and #gameroot > 0 then
            plugindir = path.join(gameroot, "Data", "SFSE", "Plugins")
        else
            cprint("${yellow}[SFSEMenuFramework] no XSE_SF_MODS_PATH or XSE_SF_GAME_PATH set; skipping post-build copy")
            return
        end

        local files_to_copy = {
            { src = target:targetfile(), label = "plugin" },
            { src = target:symbolfile(), label = "symbols" }
        }

        os.mkdir(plugindir)
        cprint("${bright cyan}[SFSEMenuFramework] copying build outputs to %s", plugindir)

        for _, entry in ipairs(files_to_copy) do
            if entry.src and os.isfile(entry.src) then
                local dst = path.join(plugindir, path.filename(entry.src))
                os.trycp(entry.src, dst)
                cprint("${green}[SFSEMenuFramework] copied %s:${clear} %s -> %s", entry.label, entry.src, dst)
            end
        end

        local fontsrc = path.join(os.projectdir(), "resources", "fonts")
        if os.isdir(fontsrc) then
            local fontdst = path.join(plugindir, "Fonts")
            os.mkdir(fontdst)
            for _, ttf in ipairs(os.files(path.join(fontsrc, "*.ttf"))) do
                local dst = path.join(fontdst, path.filename(ttf))
                os.trycp(ttf, dst)
                cprint("${green}[SFSEMenuFramework] copied font:${clear} %s -> %s", ttf, dst)
            end
        end
    end)

    before_install(function(target)
        local srcfiles, dstfiles = target:installfiles()
        if not srcfiles or #srcfiles == 0 or not dstfiles or #dstfiles == 0 then
            cprint("${yellow}[SFSEMenuFramework] no install copy targets are configured")
            return
        end

        cprint("${bright cyan}[SFSEMenuFramework] install copy plan:")
        for idx, srcfile in ipairs(srcfiles) do
            cprint("${dim}  %s -> %s", srcfile, dstfiles[idx])
        end
    end)

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
