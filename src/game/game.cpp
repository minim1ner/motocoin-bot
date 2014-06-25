// Copyright (c) 2014 The Motocoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//--------------------------------------------------------------------
// This file is part of The Game of Motocoin.
//--------------------------------------------------------------------

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#else
    #include <unistd.h>
#endif

#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <thread>
#include <mutex>
#include <string>
#include <vector>
#include <exception>
#include <sstream>
#include <debug.h>

using namespace std;
using namespace chrono;

#include "../moto-engine.h"
#include "../moto-protocol.h"
#include "vec2.hpp"
#include "graphics.hpp"
#include "render.hpp"
#include <unistd.h>

//static bool g_isRender=true
//        ;

static thread g_InputThread;
static vector<string> g_InputLines;
static mutex g_InputMutex;
int g_success=0;

// View properties.
static float g_RenderScale = 0.1f;
static float g_MapSize = 0.4f;
static bool g_ShowTimer = true;
static bool g_ShowHelp = false;
static bool g_ShowMap = true;
static bool g_OverallView = false;
static double g_Speed = 1.0;
int g_i=0;

// Sky shift.
static int32_t g_PrevIntPosition[2]; // used in sky shifting calculation
static vec2 g_SkyShift;

static MotoWork  g_Work;
static MotoWork  g_NextWork;
static MotoWorld g_World;
static MotoState g_FirstFrame;
static MotoState g_Frame;
static MotoPoW   g_PoW;

const int g_rotateRightCnt=15;
const int g_rotateLeftCnt=15;
const int g_switchCnt=5;
const int g_commandsCnt=g_rotateRightCnt+g_rotateLeftCnt+g_switchCnt;
int64_t g_finishDistSq=((int64_t)1)<<61;

static int32_t g_rotateRightPos[g_rotateRightCnt];
static int32_t g_rotateLeftPos[g_rotateLeftCnt];
static int32_t g_switchPos[g_switchCnt];
//static int32_t g_startTemp=1000;
static int32_t g_startTemp=300;
static int32_t g_k=7;
//static int32_t g_firstTemp=700;
static int32_t g_firstTemp=100;
static int32_t g_temp=0;
//static int32_t g_totalFrames=18.46*250;
static int32_t g_totalFrames=30
        *250;

static bool g_HasNextWork = false;
static bool g_PlayingForFun = true;
static bool g_SchematicMainView = false;

static bool isRender(){
    return g_PlayingForFun;
}


static enum
{
	STATE_PLAYING,
	STATE_REPLAYING,
	STATE_DEAD,
    STATE_SUCCESS,
    STATE_BRUTE
} g_State;

static int randLim(int lim){
    return lim>0?rand()%lim:0;
}

struct Command
{
    int32_t time;
    EMotoRot rotation;
    EMotoAccel acceleration;


    Command(EMotoRot r) : rotation(r) {time=randLim(g_totalFrames);acceleration=MOTO_IDLE;}
    Command(EMotoAccel ac) : acceleration(ac) {rotation=MOTO_NO_ROTATION;}
    Command(){}
};

struct less_than_time
{
    inline bool operator() (const Command& struct1, const Command& struct2)
    {
        return (struct1.time < struct2.time);
    }
};

static bool g_MotoDir = false;

static double g_PrevTime;
static double g_PlayTime;
static double g_ProgramStartTime;

static GLFWwindow* g_pWindow;

static Command g_commands[g_commandsCnt];
static Command g_bestCommands[g_commandsCnt];


static int g_rotationPeriod=200;

static void updatePow(){

    MotoState tmpFrame;
    EMotoAccel accel=MOTO_GAS_RIGHT;
    EMotoRot rotation;
    int last_rotation=0;
    int last_acceleration = 0;
    g_PoW.NumUpdates = 0;
    g_PoW.NumFrames =g_Work.TimeTarget-1;

    tmpFrame.iFrame=1.5*250;
    recordInput(&g_PoW, &tmpFrame, MOTO_GAS_RIGHT, MOTO_NO_ROTATION);
    for(int i=0;i<g_commandsCnt;i++){
        rotation=MOTO_NO_ROTATION;
        if(g_commands[i].acceleration!=MOTO_IDLE){
            if(g_commands[i].time-last_acceleration>g_rotationPeriod){
                if(accel==MOTO_GAS_RIGHT){
                    accel=MOTO_GAS_LEFT;
                }else{
                    accel=MOTO_GAS_RIGHT;
                }
                last_acceleration=g_commands[i].time;
            }
        }
        if(g_commands[i].rotation!=MOTO_NO_ROTATION){
            if(g_commands[i].time-last_rotation>g_rotationPeriod+1){
               last_rotation=g_commands[i].time;
               rotation=g_commands[i].rotation;
            }
        }

        tmpFrame.iFrame=g_commands[i].time;

        recordInput(&g_PoW, &tmpFrame, accel, rotation);
    }
}

static void regeneratePoW(){

    memcpy (g_commands, g_bestCommands, sizeof (g_bestCommands)) ;
    for(int i=max(0,randLim(g_commandsCnt+g_commandsCnt/3)-g_commandsCnt/3);i<g_commandsCnt;i++){
//    for(int i=0;i<g_commandsCnt;i++){
        g_commands[i].time+=randLim(8*g_temp-7)-4*g_temp;
        g_commands[i].time=max(0,min(g_commands[i].time,g_totalFrames+1));
    }

    sort(g_commands,g_commands+g_commandsCnt,less_than_time());

//    Command arr[g_commandsCnt];
//    std::copy(g_commands.begin(), g_commands.end(), arr);

    updatePow();

}

static CView getBigView()
{
	float Size = g_RenderScale*sqrt(g_ViewportSize.area());

	vec2 Position = vec2((float)g_Frame.Bike.Pos[0], (float)uint32_t(g_Frame.Bike.Pos[1]))*g_MotoPosK;

	vec2 p0 = Position - g_ViewportSize/Size;
	vec2 p1 = Position + g_ViewportSize/Size;
	vec2 ScreenPos[2] = { vec2(-1.0f, -1.0f), vec2(1.0f, 1.0f) };
	vec2 WorldPos[2] = { p0, p1 };

	return CView(ScreenPos, WorldPos);
}

static CView getMapView()
{
	float kxy = g_ViewportSize.x/g_ViewportSize.y;
	float kyx = g_ViewportSize.y/g_ViewportSize.x;
	vec2 ScreenPos[2] = { vec2(1.0f - g_MapSize*sqrt(kyx), -1.0f), vec2(1.0f, -1.0f + (1/1.2f)*g_MapSize*sqrt(kxy)) };

	float WorldSize = MOTO_MAP_SIZE*MOTO_SCALE;
	vec2 WorldPos[2] = { vec2(-0.6f, -0.0f)*WorldSize, vec2(0.6f, 1.0f)*WorldSize };
	return CView(ScreenPos, WorldPos);
}

static CView getOverallView()
{
	vec2 ScreenPos[2] = { vec2(-1.0f, -1.0f), vec2(1.0f, 1.0f) };

	vec2 Scale(1.0f, 1.0f);
	if (g_ViewportSize.y < g_ViewportSize.x)
		Scale.x /= g_ViewportSize.y/g_ViewportSize.x;
	else
		Scale.y /= g_ViewportSize.x/g_ViewportSize.y;

	vec2 WorldSize = Scale*MOTO_MAP_SIZE*MOTO_SCALE;
	vec2 WorldPos[2] = { vec2(-0.5f, 0.0f)*WorldSize, vec2(0.5f, 1.0f)*WorldSize };
	return CView(ScreenPos, WorldPos);
}

// Called each frame.
static void draw()
{
	//glClear(GL_COLOR_BUFFER_BIT);

	// Shift sky.
	g_SkyShift.x += 0.3f*(g_Frame.Bike.Pos[0] - g_PrevIntPosition[0])*g_MotoPosK/MOTO_SCALE;
	g_SkyShift.y += 0.3f*(g_Frame.Bike.Pos[1] - g_PrevIntPosition[1])*g_MotoPosK/MOTO_SCALE;
	g_PrevIntPosition[0] = g_Frame.Bike.Pos[0];
	g_PrevIntPosition[1] = g_Frame.Bike.Pos[1];
	
	// Render main view.
	if (!g_OverallView)
	{
		CView View = getBigView();
		drawWorldAndCoin(View, g_Frame, g_SchematicMainView, false, g_SkyShift);
		drawMoto(View, g_Frame, g_MotoDir);
	}

	// Render map view.
	if (g_ShowMap && !g_OverallView)
	{
		CView View = getMapView();
		setScissor(View.m_ScreenPos);
		drawWorldAndCoin(View, g_Frame, true, true);
		renderCyclic(View, bind(drawSchematicMoto, placeholders::_1, g_Frame));
		unsetScissor();
	}

	// Render overall view
	if (g_OverallView)
	{
		CView View = getOverallView();
		setScissor(View.m_ScreenPos);
		drawWorldAndCoin(View, g_Frame, g_SchematicMainView, false, g_SkyShift);
		renderCyclic(View, bind(drawMoto, placeholders::_1, g_Frame, g_MotoDir));
		unsetScissor();
	}
	
	if (g_ShowTimer)
	{
		int TimeLeft = g_Work.TimeTarget - g_Frame.iFrame;
		int Sec = TimeLeft / 250;
		int MilliSec = 4*(TimeLeft % 250);
		char Buffer[16];
		float LetterSize = 0.03f;
        sprintf(Buffer, "%02i.%03i:%04i:t%04i:s%02i",Sec, MilliSec,g_i,g_temp,g_success);
        drawText(Buffer, 1, -30, LetterSize, (TimeLeft == 0) ? 1 : 0);
        sprintf(Buffer, "%i", MOTO_MAX_INPUTS - g_PoW.NumUpdates);
        drawText(Buffer, 2, -30, LetterSize, (MOTO_MAX_INPUTS - g_PoW.NumUpdates == 0) ? 1 : 0);
	}

	float LetterSize = 0.02f;
	if (g_ShowHelp)
	{
		const char* pHelp =
			"General:\n"
			"    F1 - show/hide this help\n"
			"    F5 - restart current level\n"
            "    F6 - go to next level\n"
			"\n"
			"Gameplay:\n"
			"    \x1 - rotate counter-clockwise\n" 
			"    \x3 - rotate clockwise\n" 
			"    \x2 - accelerate\n" 
			"    \x4 - brake\n" 
			"    space - turnaround\n"
			"\n"
			"Time:\n"
			"    Z  - decrease time speed\n"
			"    X  - increase time speed\n"
			"    R  - rewind\n"
			"\n"
			"View:\n"
			"    S - switch view\n"
			"    + - increase scale\n"
			"    - - decrease scale\n"
			"    M - show/hide map\n"
			"    * - magnify map\n"
			"    / - reduce map\n"
			;
		drawText(pHelp, 1, 1, LetterSize);
	}
	else
		drawText("Press F1 for help", 1, 1, LetterSize, 0, 1.0f - pow(float(glfwGetTime() - g_ProgramStartTime), 3.0f)*0.005f);

	if (g_Frame.Dead)
	{
		const char* pMsg = "Press F5 to restart or R to rewind";
		drawText(pMsg, 0, 0, 1.5f*LetterSize, 1);
	}
	if (g_State == STATE_SUCCESS)
	{
		const char* pMsg = "Congratulations!!!";
		drawText(pMsg, 0, 0, 1.5f*LetterSize, 2);
	}

	drawText(g_Work.Msg, -1, 1, LetterSize);
}

// Restart current world.
static void restart()
{
    if (g_State == STATE_SUCCESS)
		return;

	if (g_State == STATE_DEAD)
		g_State = STATE_PLAYING;

	g_PrevTime = glfwGetTime();
	g_PlayTime = 0.0f;
    g_Frame = g_FirstFrame;
	if (g_State != STATE_REPLAYING)
		g_PoW.NumUpdates = 0;

	g_PrevIntPosition[0] = g_Frame.Bike.Pos[0];
	g_PrevIntPosition[1] = g_Frame.Bike.Pos[1];
}

// Inform Motocoin-Qt that we abandoned this work.
static void releaseWork(const MotoWork& Work)
{
	cout << motoMessage(Work);
}

static void goToNextWorld()
{
    g_i++;
    if (g_State == STATE_REPLAYING)
		return;

	// If there is new work then switch to it.
	if (g_HasNextWork)
	{
		releaseWork(g_Work);
		g_Work = g_NextWork;
		g_PlayingForFun = false;
		g_HasNextWork = false;
	}

	// Find next good world (some worlds are ill-formed).
    DEBUG_MSG("gen 1");
	do
        g_PoW.Nonce=rand();
    while (!motoGenerateGoodWorld(&g_World, &g_FirstFrame, g_Work.Block, &g_PoW));
		
    if(isRender()){prepareWorldRendering(g_World);}
	restart();
}

static void readStdIn()
{
	while (true)
	{
		if (!cin.good())
			return;

		string Line;
		getline(cin, Line);

		if (cin.fail())
			return;

		unique_lock<mutex> Lock(g_InputMutex);
		g_InputLines.push_back(move(Line));
	}
}

static MotoWork getWorkForFun()
{
    MotoWork Work;
    srand((unsigned int)system_clock::now().time_since_epoch().count());
    for (int i = 0; i < MOTO_WORK_SIZE; i++)
        Work.Block[i] = rand() % 256;
    Work.IsNew = false;
    Work.TimeTarget = 250*60;
    //sprintf(Work.Msg, "Block %i, Reward %f MTC, Target %.3f", Work.BlockHeight, 5000000000/100000000.0, Work.TimeTarget/250.0);
    strncpy(Work.Msg, "No work available, you may play just for fun.", sizeof(Work.Msg));

    return Work;
}

static void startBrute(){
    DEBUG_MSG("go 1");
        goToNextWorld();
        g_State = STATE_BRUTE;
        g_temp=g_startTemp;



        regeneratePoW();


        if(isRender()){prepareWorldRendering(g_World);}
        restart();
}

static void parseInput()
{
	unique_lock<mutex> Lock(g_InputMutex);

	for (const string& Line : g_InputLines)
	{
        MotoWork Work;
    MotoPoW PoW;
    if (motoParseMessage(Line.c_str(), Work))
    {
        if (g_HasNextWork)
        {
            if (g_NextWork.IsNew)
            Work.IsNew = true;
            releaseWork(g_NextWork);
        }
        g_PlayingForFun = false;
        g_NextWork = Work;
        g_HasNextWork = true;
    }else{
       if(g_PlayingForFun){
        startBrute();
       }
    }

//        }
	}

	g_InputLines.clear();
}

static bool processSolution()
{
	bool Result = g_PoW.NumFrames < g_Work.TimeTarget && motoCheck(g_Work.Block, &g_PoW);
	if (!Result)
	{
        cout << "Error: Rechecking solution failed!" << endl;
		return false;
	}

	// Print solution, it will be parsed by Motocoin-Qt.
	cout << motoMessage(g_Work, g_PoW);
	return true;
}

static void playWithInput(int NextFrame)
{
	NextFrame = min((int)g_Work.TimeTarget, NextFrame);

	// Get user input.
	static bool TurnWasAroundPressed = false;
	bool TurnAroundPressed = glfwGetKey(g_pWindow, GLFW_KEY_SPACE) == GLFW_PRESS;
	if (TurnAroundPressed && !TurnWasAroundPressed)
		g_MotoDir = !g_MotoDir;
	TurnWasAroundPressed = TurnAroundPressed;

	EMotoAccel Accel = MOTO_IDLE;
	if (glfwGetKey(g_pWindow, GLFW_KEY_DOWN) == GLFW_PRESS)
		Accel = MOTO_BRAKE;
	else if (glfwGetKey(g_pWindow, GLFW_KEY_UP) == GLFW_PRESS)
		Accel = g_MotoDir ? MOTO_GAS_LEFT : MOTO_GAS_RIGHT;

	EMotoRot Rotation = MOTO_NO_ROTATION;
	if (glfwGetKey(g_pWindow, GLFW_KEY_RIGHT) == GLFW_PRESS)
		Rotation = MOTO_ROTATE_CW;
	if (glfwGetKey(g_pWindow, GLFW_KEY_LEFT) == GLFW_PRESS)
		Rotation = MOTO_ROTATE_CCW;

	switch (motoAdvance(&g_Frame, &g_PoW, &g_World, Accel, Rotation, NextFrame - g_Frame.iFrame))
	{
	case MOTO_FAILURE:
		g_State = STATE_DEAD;
		break;

	case MOTO_SUCCESS:
        if (g_PlayingForFun){
            DEBUG_MSG("go 2");
		    goToNextWorld();
        }
		else
			g_State = processSolution() ? STATE_SUCCESS : STATE_DEAD;
		break;

	case MOTO_CONTINUE:
		break;
	}

	if (g_State != STATE_SUCCESS && g_Frame.iFrame >= g_Work.TimeTarget)
		g_Frame.Dead = true;
}

static void finalizeBatch(){
    if (g_PlayingForFun){
        DEBUG_MSG("go 3");
        goToNextWorld();
    }else{
        g_State = processSolution() ? STATE_SUCCESS : STATE_DEAD;
    }
}



// Called each frame.

static void play()
{
    double Time = glfwGetTime();
    static int numfr = 0;
    static double lasttime = 0;
    numfr++;
    if (Time - lasttime > 1)
    {
        //printf("%i\n", numfr);
        numfr = 0;
        lasttime = Time;
    }

    double TimeDelta = 0.02d;
    g_PrevTime = Time;
    if (glfwGetKey(g_pWindow, GLFW_KEY_R) == GLFW_PRESS)
    TimeDelta = -TimeDelta;
    if ((g_Frame.Dead && TimeDelta > 0) || g_State == STATE_SUCCESS)
    TimeDelta = 0.0;
    g_PlayTime += TimeDelta*g_Speed;
    if (g_PlayTime < 0.0f)
    g_PlayTime = 0.0f;

    int NextFrame = int(g_PlayTime/0.004);
    if (NextFrame == g_Frame.iFrame)
    return;

    if (NextFrame < g_Frame.iFrame&&g_State != STATE_BRUTE)
    {
        g_Frame = g_FirstFrame;
        if (g_State != STATE_REPLAYING)
        motoCutPoW(&g_PoW, NextFrame);
        if (g_State == STATE_DEAD)
        g_State = STATE_PLAYING;
        motoReplay(&g_Frame, &g_PoW, &g_World, NextFrame);
        return;
    }

    if (g_State == STATE_REPLAYING)
    {
        g_Frame = g_FirstFrame;
        motoReplay(&g_Frame, &g_PoW, &g_World, NextFrame);
        if (NextFrame >= g_PoW.NumFrames||g_Frame.curState==MOTO_FAILURE){
            g_State = STATE_BRUTE;
//            restart();
        }

        if (g_Frame.Accel == MOTO_GAS_LEFT)
        g_MotoDir = true;
        if (g_Frame.Accel == MOTO_GAS_RIGHT)
        g_MotoDir = false;
    }else if (g_State == STATE_BRUTE)
    {

        g_temp--;
        int k=g_k;
        if(g_finishDistSq<87412622){
            k=g_k*2;
        }
        if(g_finishDistSq<28088677){
            k=g_k*4;
        }

        for(int i=0;i<k;i++){
		g_Frame = g_FirstFrame;
            if(g_temp>0&&g_i<10000){
    //            g_i++;
                motoReplay(&g_Frame, &g_PoW, &g_World, g_totalFrames);
                if(g_Frame.curState==MOTO_SUCCESS){

                    g_success++;
                    g_temp=0;
                    DEBUG_MSG("success: "<<g_finishDistSq<<"time:"<<g_Frame.iFrame/150);
                    finalizeBatch();
//                    for(int i=60;i>0;i--){
//                        usleep(1000000);
//                        DEBUG_MSG("sleep: "<<i);
//                    }
                    startBrute();
                    break;
                }

                if(g_Frame.finishDistSq<g_finishDistSq){
                    if(g_Frame.finishDistSq<87412622&&g_finishDistSq>87412622){//temporary
                        g_temp+=g_firstTemp;
                    }
                    if(g_Frame.finishDistSq<28088677&&g_finishDistSq>28088677){
                        g_temp+=g_firstTemp;
                    }
                    if(g_Frame.finishDistSq<5088677&&g_finishDistSq>5088677){
                        g_temp+=g_firstTemp;
                    }

                    
//                    if(){}
                    g_finishDistSq=g_Frame.finishDistSq;
                    memcpy (g_bestCommands, g_commands, sizeof (g_commands)) ;
//                    g_State=STATE_REPLAYING;
//                    g_temp+=g_tempAddition;
                    
                    
                    g_PlayTime=0;
                }else{
                    regeneratePoW();
                    if(i==0&&rand()%100==0){
                        //cout<<"failure: "<<g_finishDistSq<<"temp:"<<g_temp<<endl;
                    }
                }

    //            restart();
    //            cout << "replay true" << NextFrame<<endl;
            }else{
                if(isRender()){
                    g_temp=0;
                    memcpy (g_commands, g_bestCommands, sizeof (g_bestCommands)) ;
                    g_State=STATE_REPLAYING;
                    g_PlayTime=0;
                }else{
//                    cout<<"failure: "<<g_finishDistSq<<"temp:"<<g_temp<<endl;
                    g_temp=g_startTemp;
                    g_finishDistSq=((int64_t)1)<<33;

                    DEBUG_MSG("go 4");
                    goToNextWorld();
                }
                break;
            }
        }
    }
	else if (g_State == STATE_PLAYING)
        playWithInput(NextFrame);
}



static void onWindowResize(GLFWwindow *pWindow, int Width, int Height)
{
	glViewport(0, 0, Width, Height);
	g_ViewportSize.x = (float)Width;
	g_ViewportSize.y = (float)Height;
}

static void changeScale(float K)
{
	if (g_OverallView || g_RenderScale*K > 0.5f || g_RenderScale*K < 0.01f)
		return;

	g_SkyShift = g_SkyShift/K;
	g_RenderScale *= K;
}

static void onKeyPress(GLFWwindow* pWindow, int Key, int Scancode, int Action, int Mods)
{
	if (Action != GLFW_PRESS)
		return;

	switch (Key)
	{
	case GLFW_KEY_F1:
		g_ShowHelp = !g_ShowHelp;
		break;

	case GLFW_KEY_F5:
		restart();
		break;

	case GLFW_KEY_F6:

        DEBUG_MSG("go 6");
		goToNextWorld();
		break;

	case GLFW_KEY_S:
		g_OverallView = !g_OverallView;
		break;

	case GLFW_KEY_M:
		g_ShowMap = !g_ShowMap;
		break;

	case GLFW_KEY_KP_ADD:
	case GLFW_KEY_EQUAL:
		changeScale(1.1f);
		break;

	case GLFW_KEY_KP_SUBTRACT:
	case GLFW_KEY_MINUS:
		changeScale(1/1.1f);
		break;

	case GLFW_KEY_KP_MULTIPLY:
		g_MapSize *= 1.1f;
		break;

	case GLFW_KEY_KP_DIVIDE:
		g_MapSize /= 1.1f;
		break;

	case GLFW_KEY_Z:
		g_Speed = max(g_Speed/1.1, 0.3);
		break;

	case GLFW_KEY_X:
		g_Speed = min(g_Speed*1.1, 3.0);
		break;
	}
}

void showError(const string& Error)
{
	cout << Error << endl;
	string Error2 = Error + "\nTry to use software rendering.";
#ifdef _WIN32
	MessageBoxA(NULL, Error2.c_str(), "Motogame Error", MB_ICONERROR);
#endif
}

static void GLFW_ErrorCallback(int iError, const char* pString)
{
	stringstream Stream;
	Stream << "GLFW Error #" << iError << ": " << pString;
	showError(Stream.str());
}

int main(int argc, char** argv)
{
    g_PoW.NumFrames=g_totalFrames;

    for(int i=0;i<g_rotateLeftCnt;i++){
        g_commands[i]=Command(MOTO_ROTATE_CCW);
    }
    for(int i=0;i<g_rotateRightCnt;i++){
        g_commands[i+g_rotateLeftCnt]=(Command(MOTO_ROTATE_CW));
    }
    for(int i=0;i<g_switchCnt;i++){
        g_commands[i+g_rotateLeftCnt+g_rotateRightCnt]=Command(MOTO_GAS_RIGHT);
    }

    memcpy (g_bestCommands, g_commands, sizeof (g_commands));


    bool NoFun = false;
    for (int i = 0; i < argc; i++)
    {
        if (strcmp(argv[i], "-nofun") == 0){
            NoFun = true;
            g_PlayingForFun=false;
        }else{

        }
        if (strcmp(argv[i], "-schematic") == 0)
            g_SchematicMainView = true;
    }


    // Initialize GLFW library
    glfwSetErrorCallback(GLFW_ErrorCallback);
    if (!glfwInit())
    return -1;

    // Create a OpenGL window
    const int Width = 800;
    const int Height = 600;
    g_pWindow = glfwCreateWindow(Width, Height, "The Game of Motocoin", NULL, NULL);
    if (!g_pWindow)
    {
    showError ("glfwCreateWindow failed");
    glfwTerminate();
    return -1;
    }

    // Make the window's context current
    glfwMakeContextCurrent(g_pWindow);

    // Set window callbacks
    glfwSetKeyCallback(g_pWindow, onKeyPress);
    glfwSetWindowSizeCallback(g_pWindow, onWindowResize);

    // GLFW doesn't call window resize callback when window is created so we call it.
    onWindowResize(g_pWindow, Width, Height);

    // Enable vsync. Doesn't work on Windows.
    glfwSwapInterval(1);

    // Initialize our world rendering stuff.
    initRenderer();

    g_ProgramStartTime = glfwGetTime();

    motoInitPoW(&g_PoW);

    // Let's start to play.
    g_Work = getWorkForFun();

    DEBUG_MSG("go 7");

    if(g_PlayingForFun){
        g_State = STATE_PLAYING;
    }else{
        startBrute();
    }


    goToNextWorld();

    g_InputThread = thread(readStdIn);

    #if 0
    auto PrevTime = steady_clock::now();
    #endif
    int PrevTime = int(glfwGetTime()*1000);

    // Loop until the user closes the window.
    while (!glfwWindowShouldClose(g_pWindow))
    {
    parseInput();

    if (g_HasNextWork && (g_PlayingForFun || g_NextWork.IsNew)) // New block was found, our current work was useless, switch to new work.
    {
//    g_State = STATE_PLAYING;

    DEBUG_MSG("go 8");
    goToNextWorld();
    }

    if (!(g_PlayingForFun && NoFun))
    {
//        for(int i=0;i<10;i++){
    play();
//        }
    draw();
    }

    glfwSwapBuffers(g_pWindow);
    glfwPollEvents();

    // 1. glfwSwapInterval doesn't work on Windows.
    // 2. It may also not work on other platforms for some reasons (e.g. GPU settings).
    // So we are limiting FPS to 100 to not overuse CPU and GPU.
    #if 0
    auto Time = steady_clock::now();
    auto TimeToWait = milliseconds(10) - (Time - PrevTime);
    if (TimeToWait.count() > 0)
    this_thread::sleep_for(TimeToWait);
    PrevTime = steady_clock::now();
    #endif
    int Time = int(glfwGetTime()*1000);
    int TimeToWait = 10 - (Time - PrevTime);
    if (TimeToWait > 0)
    this_thread::sleep_for(milliseconds(TimeToWait));
    PrevTime = int(glfwGetTime()*1000);
    }

    glfwTerminate();

    // g_InputThread is still running and there is no way to terminate it.
    #ifdef _WIN32
    TerminateProcess(GetCurrentProcess(), 0);
    #else
    _exit(0);
    #endif

    return 0;
}
