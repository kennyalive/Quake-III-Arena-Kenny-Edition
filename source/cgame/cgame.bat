rem make sure we have a safe environement
set LIBRARY=
set INCLUDE=

mkdir ..\..\intermediate\vm\cgame
cd ..\..\intermediate\vm\cgame

set PATH=..\..\..\tools\bin;%PATH%

set src=..\..\..\source
set cc=lcc -DQ3_VM -S -Wf-target=bytecode -Wf-g -I%src%\cgame -I%src%\game -I%src%\ui %1

%cc% %src%/game/bg_misc.c
@if errorlevel 1 goto quit
%cc% %src%/game/bg_pmove.c
@if errorlevel 1 goto quit
%cc% %src%/game/bg_slidemove.c
@if errorlevel 1 goto quit
%cc% %src%/game/bg_lib.c
@if errorlevel 1 goto quit
%cc% %src%/game/q_math.c
@if errorlevel 1 goto quit
%cc% %src%/game/q_shared.c
@if errorlevel 1 goto quit
%cc% %src%/cgame/cg_consolecmds.c
@if errorlevel 1 goto quit
%cc% %src%/cgame/cg_draw.c
@if errorlevel 1 goto quit
%cc% %src%/cgame/cg_drawtools.c
@if errorlevel 1 goto quit
%cc% %src%/cgame/cg_effects.c
@if errorlevel 1 goto quit
%cc% %src%/cgame/cg_ents.c
@if errorlevel 1 goto quit
%cc% %src%/cgame/cg_event.c
@if errorlevel 1 goto quit
%cc% %src%/cgame/cg_info.c
@if errorlevel 1 goto quit
%cc% %src%/cgame/cg_localents.c
@if errorlevel 1 goto quit
%cc% %src%/cgame/cg_main.c
@if errorlevel 1 goto quit
%cc% %src%/cgame/cg_marks.c
@if errorlevel 1 goto quit
%cc% %src%/cgame/cg_players.c
@if errorlevel 1 goto quit
%cc% %src%/cgame/cg_playerstate.c
@if errorlevel 1 goto quit
%cc% %src%/cgame/cg_predict.c
@if errorlevel 1 goto quit
%cc% %src%/cgame/cg_scoreboard.c
@if errorlevel 1 goto quit
%cc% %src%/cgame/cg_servercmds.c
@if errorlevel 1 goto quit
%cc% %src%/cgame/cg_snapshot.c
@if errorlevel 1 goto quit
%cc% %src%/cgame/cg_view.c
@if errorlevel 1 goto quit
%cc% %src%/cgame/cg_weapons.c
@if errorlevel 1 goto quit

q3asm -f %src%/cgame/cgame
:quit
cd %src%/cgame
