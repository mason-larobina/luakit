table.insert( mode_binds['normal'], lousy.bind.buf("^za",                     function (w) w:formfiller("add") end) )
table.insert( mode_binds['normal'], lousy.bind.buf("^zn",                     function (w) w:formfiller("new") end) )
table.insert( mode_binds['normal'], lousy.bind.buf("^ze",                     function (w) w:formfiller("edit") end) )
table.insert( mode_binds['normal'], lousy.bind.buf("^zl",                     function (w) w:formfiller("load") end) )

window_helpers["formfiller"] = function(w, action)
        local editor = (os.getenv("EDITOR") or "vim") .. " "
        local modeline = "> vim:ft=formfiller"
        local filename = ""
        local formsDir = luakit.data_dir .. "/forms/"
        luakit.spawn(string.format("mkdir -p %q", formsDir))
        if action == "once" then
            filename = os.tmpname()
        else
            local uri, match = string.gsub(string.gsub(w.sbar.l.uri.text, "%w+://", ""), "(.-)/.*", "%1")
            filename = formsDir .. uri
        end 
        if action == "new" or action == "once" or action == "add" then
            local dumpFunction=[[(function dump() {
                var rv='';
                var allFrames = new Array(window);
                for(f=0;f<window.frames.length;f=f+1) {
                    allFrames.push(window.frames[f]);
                }
                for(j=0;j<allFrames.length;j=j+1) {
                    try {
                        for(f=0;f<allFrames[j].document.forms.length;f=f+1) {
                            var fn = allFrames[j].document.forms[f].name;
                            var fi = allFrames[j].document.forms[f].id;
                            var fm = allFrames[j].document.forms[f].method;
                            var form = '!form[' + fn + '|' + fi + '|' + fm +']:autosubmit=0\n';
                            var fb = '';
                                var xp_res=allFrames[j].document.evaluate('.//input', allFrames[j].document.forms[f], null, XPathResult.ANY_TYPE,null);
                                var input;
                                while(input=xp_res.iterateNext()) {
                                    if(input.name != "") {
                                        var type=(input.type?input.type:text);
                                        if(type == 'text' || type == 'password' || type == 'search') {
                                            fb += input.name + '(' + type + '):' + input.value + '\n';
                                        }
                                        else if(type == 'checkbox' || type == 'radio') {
                                            fb += input.name + '{' + input.value + '}(' + type + '):' + (input.checked?'ON':'OFF') + '\n';
                                        }
                                    }
                                }
                                xp_res=allFrames[j].document.evaluate('.//textarea', allFrames[j].document.forms[f], null, XPathResult.ANY_TYPE,null);
                                var input;
                                while(input=xp_res.iterateNext()) {
                                    if(input.name != "") {
                                        fb += input.name + '(textarea):' + input.value + '\n';
                                    }
                                }
                            if(fb.length) {
                                rv += form + fb;
                            }
                        }
                    }
                    catch(err) { }
                }
                return rv;
            })()]]
            math.randomseed(os.time())
            if action == "add" and os.exists(filename) then
                modeline = ""
            end
            local fd
            if action == "add" then
                fd = io.open(filename, "a+")
            else
                fd = io.open(filename, "w+")
            end
            fd:write(string.format("%s\n!profile=NAME_THIS_PROFILE_%d\n%s", modeline, math.random(1,9999), w:get_current():eval_js(dumpFunction, "dump")))
            fd:flush()
            luakit.spawn("xterm -e " .. editor .. filename)
            fd:close()
        elseif action == "load" then
            local insertFunction=[[function insert(fname, ftype, fvalue, fchecked) {
                var allFrames = new Array(window);
                for(f=0;f<window.frames.length;f=f+1) {
                    allFrames.push(window.frames[f]);
                }
                for(j=0;j<allFrames.length;j=j+1) {
                    try {
                        if(ftype == 'text' || ftype == 'password' || ftype == 'search' || ftype == 'textarea') {
                            allFrames[j].document.getElementsByName(fname)[0].value = fvalue;
                        }
                        else if(ftype == 'checkbox') {
                            allFrames[j].document.getElementsByName(fname)[0].checked = fchecked;
                        }
                        else if(ftype == 'radio') {
                            var radios = allFrames[j].document.getElementsByName(fname);
                            for(r=0;r<radios.length;r+=1) {
                                if(radios[r].value == fvalue) {
                                    radios[r].checked = fchecked;
                                }
                            }
                        }
                    }
                    catch(err) { }
                }
            };]]
            local submitFunction=[[function submitForm(fname, fid, fmethod) {
                var allFrames = new Array(window);
                for(f=0;f<window.frames.length;f=f+1) {
                    allFrames.push(window.frames[f]);
                }
                for(j=0;j<allFrames.length;j=j+1) {
                    for(f=0;f<allFrames[j].document.forms.length;f=f+1) {
                        var myForm = allFrames[j].document.forms[f];
                        if( ( (myForm.name != "" && myForm.name == fname) || (myForm.id != "" && myForm.id == fid)) && myForm.method == fmethod) {
                            myForm.submit();
                            return;
                        }
                    }
                }
            };]]
            local fd, err = io.open(filename, "r")
            if not fd then
                return nil
            end
            local profile = ""
            fd:seek("set")
            for l in fd:lines() do
                if string.match(l, "^!profile=.*$") then
                    if profile == "" then
                        profile = string.format("%s", string.match(l, "^!profile=([^$]*)$"))
                    else
                        profile = string.format("%s\n%s", profile, string.match(l, "^!profile=([^$]*)$"))
                    end
                end
            end
            if profile:find("\n") then
                local exit_status, multiline, err = luakit.spawn_sync('sh -c \'if [ "`dmenu --help 2>&1| grep lines`x" != "x" ]; then echo -n "-l 3"; else echo -n ""; fi\'')
                if exit_status ~= 0 then
                    print(string.format("An error occured: %s", err))
                    return nil
                end
                -- color settings
                local NB="#0f0f0f"
                local NF="#4e7093"
                local SB="#003d7c"
                local SF="#3a9bff"
                profile = string.format('sh -c \'echo -e -n "%s" | dmenu %s -nb "%s" -nf "%s" -sb "%s" -sf "%s" -p "Choose profile"\'', profile, multiline, NB, NF, SB, SF)
                exit_status, profile, err = luakit.spawn_sync(profile)
                if exit_status ~= 0 then
                    print(string.format("An error occured: ", err))
                    return nil
                end
            end
            fd:seek("set")
            for line in fd:lines() do
                if string.match(line, "^!profile=" .. profile .. "\ *$") then
                    break
                end
            end
            local fname, fchecked, ftype, fvalue, form
            local autosubmit = 0
            local js = string.format("%s %s", insertFunction, submitFunction)
            local pattern1 = "(.+)%((.+)%):% *(.*)"
            local pattern2 = "%1{0}(%2):%3"
            local pattern3 = "([^{]+){(.+)}%((.+)%):% *(.*)"
            for line in fd:lines() do
                if not string.match(line, "^!profile=.*") then
                    if string.match(line, "^!form.*") and autosubmit == "1" then
                        break
                    end
                    if string.match(line, "^!form.*") then
                        form = line
                        autosubmit = string.match(form, "^!form%[.-%]:autosubmit=(%d)")
                    else
                        if ftype == "textarea" then
                            if string.match(string.gsub(line, pattern1, pattern2), pattern3) then
                                js = string.format("%s insert('%s', '%s', '%s', '%s');", js, fname, ftype, fvalue, fchecked)
                                ftype = nil
                            else
                                fvalue = string.format("%s\\n%s", fvalue, line)
                            end
                        end
                        if ftype ~= "textarea" then
                            fname, fchecked, ftype, fvalue = string.match(string.gsub(line, pattern1, pattern2), pattern3)
                            if fname ~= nil and ftype ~= "textarea" then
                                js = string.format("%s insert('%s', '%s', '%s', '%s');", js, fname, ftype, fvalue, fchecked)
                            end
                        end
                    end
                else
                    break
                end
            end
            if ftype == "textarea" then
                js = string.format("%s insert('%s', '%s', '%s', '%s');", js, fname, ftype, fvalue, fchecked)
            end
            if autosubmit == "1" then
                js = string.format("%s submitForm('%s', '%s', '%s');", js, string.match(form, "^!form%[([^|]-)|([^|]-)|([^|]-)%]"))
            end
            w:get_current():eval_js(js, "f")
            fd:close()
        elseif action == "edit" then
            luakit.spawn(string.format("xterm -e %s %s", editor, filename))
        end
    end
