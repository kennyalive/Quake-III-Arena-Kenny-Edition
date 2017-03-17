rem make sure we have a safe environement
set LIBRARY=
set INCLUDE=

mkdir ..\..\intermediate\vm\ui
cd ..\..\intermediate\vm\ui

set PATH=..\..\..\tools\bin;%PATH%

set src=..\..\..\source
set cc=lcc -DQ3_VM -S -Wf-target=bytecode -Wf-g -I%src%\cgame -I%src%\game -I%src%\ui %1

%cc% %src%/q3_ui/ui_main.c
@if errorlevel 1 goto quit
%cc% %src%/q3_ui/ui_cdkey.c
@if errorlevel 1 goto quit
%cc% %src%/q3_ui/ui_ingame.c
@if errorlevel 1 goto quit
%cc% %src%/q3_ui/ui_serverinfo.c
@if errorlevel 1 goto quit
%cc% %src%/q3_ui/ui_confirm.c
@if errorlevel 1 goto quit
%cc% %src%/q3_ui/ui_setup.c
@if errorlevel 1 goto quit
%cc% %src%/game/bg_misc.c
@if errorlevel 1 goto quit
%cc% %src%/game/bg_lib.c
@if errorlevel 1 goto quit
%cc% %src%/game/q_math.c
@if errorlevel 1 goto quit
%cc% %src%/game/q_shared.c
@if errorlevel 1 goto quit
%cc% %src%/q3_ui/ui_gameinfo.c
@if errorlevel 1 goto quit
%cc% %src%/q3_ui/ui_atoms.c
@if errorlevel 1 goto quit
%cc% %src%/q3_ui/ui_connect.c
@if errorlevel 1 goto quit
%cc% %src%/q3_ui/ui_controls2.c
@if errorlevel 1 goto quit
%cc% %src%/q3_ui/ui_demo2.c
@if errorlevel 1 goto quit
%cc% %src%/q3_ui/ui_mfield.c
@if errorlevel 1 goto quit
%cc% %src%/q3_ui/ui_credits.c
@if errorlevel 1 goto quit
%cc% %src%/q3_ui/ui_menu.c
@if errorlevel 1 goto quit
%cc% %src%/q3_ui/ui_options.c
@if errorlevel 1 goto quit
%cc% %src%/q3_ui/ui_display.c
@if errorlevel 1 goto quit
%cc% %src%/q3_ui/ui_sound.c
@if errorlevel 1 goto quit
%cc% %src%/q3_ui/ui_network.c
@if errorlevel 1 goto quit
%cc% %src%/q3_ui/ui_playermodel.c
@if errorlevel 1 goto quit
%cc% %src%/q3_ui/ui_players.c
@if errorlevel 1 goto quit
%cc% %src%/q3_ui/ui_playersettings.c
@if errorlevel 1 goto quit
%cc% %src%/q3_ui/ui_preferences.c
@if errorlevel 1 goto quit
%cc% %src%/q3_ui/ui_qmenu.c
@if errorlevel 1 goto quit
%cc% %src%/q3_ui/ui_servers2.c
@if errorlevel 1 goto quit
%cc% %src%/q3_ui/ui_sparena.c
@if errorlevel 1 goto quit
%cc% %src%/q3_ui/ui_specifyserver.c
@if errorlevel 1 goto quit
%cc% %src%/q3_ui/ui_splevel.c
@if errorlevel 1 goto quit
%cc% %src%/q3_ui/ui_sppostgame.c
@if errorlevel 1 goto quit
%cc% %src%/q3_ui/ui_startserver.c
@if errorlevel 1 goto quit
%cc% %src%/q3_ui/ui_team.c
@if errorlevel 1 goto quit
%cc% %src%/q3_ui/ui_video.c
@if errorlevel 1 goto quit
%cc% %src%/q3_ui/ui_cinematics.c
@if errorlevel 1 goto quit
%cc% %src%/q3_ui/ui_spskill.c
@if errorlevel 1 goto quit
%cc% %src%/q3_ui/ui_addbots.c
@if errorlevel 1 goto quit
%cc% %src%/q3_ui/ui_removebots.c
@if errorlevel 1 goto quit
%cc% %src%/q3_ui/ui_teamorders.c
@if errorlevel 1 goto quit
%cc% %src%/q3_ui/ui_mods.c
@if errorlevel 1 goto quit

q3asm -f %src%/q3_ui/q3_ui
:quit
cd %src%/q3_ui
