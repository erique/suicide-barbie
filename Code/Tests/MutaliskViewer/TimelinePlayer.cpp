/*
 * Test of the Screen class
 */
//#include <mutant/reader.h>
//#include <mutant/io_factory.h>

#include <pspkernel.h>
#include <pspdisplay.h>
#include <psprtc.h>
#include <pspdebug.h>
#include <pspctrl.h>
#include <pspgu.h>
#include <pspgum.h>

#include "callbacks.h"
#include "vram.h"
#include "AnimCreator.h"
#include <psppower.h>

#include <mutalisk/psp/pspPlatform.h>
#include <mutalisk/mutalisk.h>
#include "dlmalloc.h"
// #include "new.h"

extern "C" {
	#include <pspsuspend.h>
	#include <Base/Math/Math.h>
	#include <Base/Std/Std.h>
	#include <Base/Math/Lin.h>
	#include <Base/Math/Quat.h>

	extern uint32_t allocated_memory;
}


#include <player/TimeControl.h>

#include <pspsdk.h>

volatile bool gStartMusic = false;

void streamWaveFile(const char *file);
void streamWavePause(int pause);
void streamWaveNudge(int offset);
void streamAT3File(const char *file);

//#define streamWavePause(x) 
//#define streamWaveNudge(x) 

#include "intro.h"

PSP_MODULE_INFO("Suicide Barbie", PSP_MODULE_KERNEL, 1, 1);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);
PSP_HEAP_SIZE_KB(1);

static unsigned int __attribute__((aligned(16))) list[2][262144];
extern unsigned char logo_start[];


#define BUF_WIDTH (512)
#define SCR_WIDTH (480)
#define SCR_HEIGHT (272)

std::string gPathPrefix = "ms0:BarbieData/";
//std::string gPathPrefix = "host1:DemoTest/";


#include "TestDemo.h"
#include "TimeBlock.h"
#include "PostProcess.h"
std::auto_ptr<TestDemo> gDemo;
/*
struct Texture
{
	int format;
	int mipmap;
	int width, height, stride;
	void* data;
	void* vramAddr;
};

int gViewportWidth;
int gViewportHeight;
void setRenderTarget(Texture& renderTarget)
{
	sceGuDrawBufferList(renderTarget.format,renderTarget.vramAddr,renderTarget.stride);
	sceGuOffset(2048 - (renderTarget.width/2),2048 - (renderTarget.height/2));
	sceGuViewport(2048,2048,renderTarget.width,renderTarget.height);

	gViewportWidth = renderTarget.width;
	gViewportHeight = renderTarget.height;
}*/

u64 gPrevTick = 0;
float getDeltaTime()
{
	u64 currTick;
	sceRtcGetCurrentTick(&currTick);
	u64 delta = currTick - gPrevTick;
	gPrevTick = currTick;
	float realTime = static_cast<float>((delta) * mutalisk::tickFrequency()) / (1000.0f * 1000.0f);
	if (s_createAnim)
	{
		float simTime = 1.f / 60.f;
		float ratio = simTime / realTime;
		printf("simulating @ %3.1f%% of realtime\n", ratio * 100.f);
		return simTime;
	}
	else
	{
		return realTime;
	}
}

void bloom(mutalisk::Texture& mainRenderTarget, mutalisk::Texture& renderTarget, mutalisk::Texture& renderTarget2,
	float blurStrength, unsigned blurThreshold, unsigned blurSrcModifier, unsigned blurDstModifier, unsigned quality) 
{
	{
		mutalisk::setRenderTarget(renderTarget);

		unsigned clearWithBlurThreshold = 0xff000000 |
			(blurThreshold << 16) | (blurThreshold << 8) | blurThreshold;
		sceGuClearColor(clearWithBlurThreshold);
		sceGuClearDepth(0xffff);
		sceGuClear(GU_COLOR_BUFFER_BIT|GU_DEPTH_BUFFER_BIT);

		mutalisk::Texture mainRenderTargetAsSource;
		mainRenderTargetAsSource = mainRenderTarget;
		mainRenderTargetAsSource.width = mainRenderTarget.stride;
		mainRenderTargetAsSource.height = mainRenderTarget.stride;
		mainRenderTargetAsSource.data = mapVramBufferToTexture(mainRenderTarget.vramAddr);

		mutalisk::Sampler sampler;
		sampler.addressU = GU_CLAMP;
		sampler.addressV = GU_CLAMP;
		sampler.minFilter = GU_LINEAR;//GU_NEAREST;
		sampler.magFilter = GU_NEAREST;

		mutalisk::Region uvRegion;
		uvRegion.offset.x = uvRegion.offset.y = 0;
		uvRegion.scale.x = (float)SCR_WIDTH/(float)BUF_WIDTH;
		uvRegion.scale.y = (float)SCR_HEIGHT/(float)BUF_WIDTH;

		mutalisk::pushState();
			sceGuEnable(GU_BLEND);
			sceGuBlendFunc(GU_SUBTRACT, GU_FIX, GU_FIX, 0xffffffff, 0xffffffff);
			drawFullscreenQuad(mainRenderTargetAsSource, sampler, uvRegion);
			sceGuDisable(GU_BLEND);

			//mutalisk::gpuBlur(renderTarget, renderTarget2, blurStrength);
			mutalisk::gpuBlurFast(renderTarget, renderTarget2, blurStrength, quality);
		mutalisk::popState();
	}

	{
		mutalisk::setRenderTarget(mainRenderTarget);

		mutalisk::Sampler sampler;
		sampler.addressU = GU_CLAMP;
		sampler.addressV = GU_CLAMP;
		sampler.minFilter = GU_LINEAR;
		sampler.magFilter = GU_LINEAR;

		mutalisk::pushState();
			sceGuEnable(GU_BLEND);
			unsigned int srcFix = GU_ARGB(0, blurSrcModifier, blurSrcModifier, blurSrcModifier);
			unsigned int dstFix = GU_ARGB(0, blurDstModifier, blurDstModifier, blurDstModifier);
			sceGuBlendFunc(GU_ADD, GU_FIX, GU_FIX, srcFix, dstFix);
			mutalisk::drawFullscreenQuad(renderTarget, sampler);
			sceGuDisable(GU_BLEND);
		mutalisk::popState();
	}
}

mutalisk::TimeControl gTimeControl;
extern "C"
{
	extern void* gVolatileMem;
	extern int gVolatileMemSize;
}
int main(int argc, char* argv[])
{
	setupCallbacks();

/*
	sceKernelVolatileMemLock(0, &gVolatileMem, &gVolatileMemSize);
	printf("gVolatileMem = %x ; gVolatileMemSize = %i\n", (unsigned)gVolatileMem, gVolatileMemSize);
*/

	// setup GU
	mutalisk::Texture mainRenderTarget;
	mainRenderTarget.format = GU_PSM_8888;
	mainRenderTarget.mipmap = 0;
	mainRenderTarget.width = SCR_WIDTH;
	mainRenderTarget.height = SCR_HEIGHT;
	mainRenderTarget.stride = BUF_WIDTH;
	mainRenderTarget.vramAddr = getStaticVramBuffer(mainRenderTarget.stride,mainRenderTarget.height,mainRenderTarget.format);
	mainRenderTarget.data = 0;

	mutalisk::Texture mainRenderTarget2 = mainRenderTarget;
	mainRenderTarget2.vramAddr = getStaticVramBuffer(mainRenderTarget2.stride,mainRenderTarget2.height,mainRenderTarget2.format);

	void* zbp = getStaticVramBuffer(BUF_WIDTH,SCR_HEIGHT,GU_PSM_4444);

	// Post process render targets
	std::auto_ptr<mutalisk::Texture> renderTarget;
	std::auto_ptr<mutalisk::Texture> renderTarget2;
	allocPostProcessTargets(GU_PSM_8888, 128, 128, renderTarget, renderTarget2);

	pspDebugScreenInit();
	sceGuInit();

	sceGuStart(GU_DIRECT,list);
	sceGuDrawBuffer(mainRenderTarget.format,mainRenderTarget.vramAddr,mainRenderTarget.stride);
	sceGuDispBuffer(mainRenderTarget2.width,mainRenderTarget2.height,mainRenderTarget2.vramAddr,mainRenderTarget2.stride);
	sceGuDepthBuffer(zbp,BUF_WIDTH);
	sceGuOffset(2048 - (mainRenderTarget.width/2),2048 - (mainRenderTarget.height/2));
	sceGuViewport(2048,2048,mainRenderTarget.width,mainRenderTarget.height);
	sceGuDepthRange(0, 0xffff);
	sceGuScissor(0,0,mainRenderTarget.width,mainRenderTarget.height);
	sceGuEnable(GU_SCISSOR_TEST);
	sceGuEnable(GU_DEPTH_TEST);
	sceGuDepthFunc(GU_EQUAL);
//	sceGuFrontFace(GU_CW);
	sceGuFrontFace(GU_CCW);
	sceGuShadeModel(GU_SMOOTH);
	sceGuEnable(GU_CULL_FACE);
	sceGuDisable(GU_TEXTURE_2D);

	sceGuFinish();
	sceGuSync(0,0);

	sceDisplayWaitVblankStart();
	sceGuDisplay(GU_TRUE);

	// run sample
	if (!loadIntro(gPathPrefix + "intro/psp/"))
		printf("error loading intro\n");

	if (sizeof(IntroRenderTarget) != sizeof(mutalisk::Texture))
	{
		printf("ERROR: mismatching render target structure size\n");
	}
	IntroRenderTarget irt[2];
	memcpy(&irt[0], &mainRenderTarget, sizeof(mutalisk::Texture));
	memcpy(&irt[1], &mainRenderTarget2, sizeof(mutalisk::Texture));
	SceUID intro = startIntro(irt);

	int val = 0;

	printf("ScenePlayer: create\n");
//	scenePlayerApp.reset(new ScenePlayerApp(gSceneFileName, gPathPrefix));
	gDemo.reset(new TestDemo());
	gDemo->setPath(gPathPrefix);
	gDemo->platformSetup();
	gDemo->start();
	printf("ScenePlayer: created and loaded\n");

	sceKernelWaitThreadEnd(intro, 0);
	sceKernelDeleteThread(intro);
	unloadIntro();

	SceCtrlData oldPad;
	oldPad.Buttons = 0;
	sceCtrlSetSamplingCycle(0);
	sceCtrlSetSamplingMode(0); 


	mutalisk::initTickFrequency(); getDeltaTime();
;;printf("tickResolution: %f, tickFrequency: %f\n", mutalisk::tickResolution(), mutalisk::tickFrequency());

	gStartMusic = true;

//;;mutalisk::TimeBlock updateTime, /*processTime, */ loopTime, renderTime, finishAndSyncTime;

	bool doPrintInfo = false;
	gTimeControl.restart(true);
	while(running())
	{
		SceCtrlData pad;
		if(sceCtrlPeekBufferPositive(&pad, 1) && false)
		{
			float speedModifier = 1.0f;
			{
				if (pad.Buttons & PSP_CTRL_CROSS)
					speedModifier = 0.25;

				if (pad.Buttons & PSP_CTRL_UP)
					gTimeControl.restart(true);
				if (pad.Buttons & PSP_CTRL_DOWN)
					gTimeControl.resetKeys();

				if (pad.Buttons & PSP_CTRL_CIRCLE)
					gTimeControl.restart(false);

				if (pad.Buttons & PSP_CTRL_SQUARE)
					gTimeControl.from(gTimeControl.time());
				if (pad.Buttons & PSP_CTRL_TRIANGLE)				
					gTimeControl.to(gTimeControl.time());

				if ((pad.Buttons & PSP_CTRL_START) && !(oldPad.Buttons & PSP_CTRL_START))
					doPrintInfo = !doPrintInfo;
				if ((pad.Buttons & PSP_CTRL_SELECT) && !(oldPad.Buttons & PSP_CTRL_SELECT))
				{
					gTimeControl.pause(!gTimeControl.isPaused());
					streamWavePause(gTimeControl.isPaused());
				}


				if (pad.Buttons & PSP_CTRL_LEFT)
					gTimeControl.scroll(-5.0f * speedModifier);
				if (pad.Buttons & PSP_CTRL_RIGHT)
					gTimeControl.scroll(5.0f * speedModifier);
				if (pad.Buttons & PSP_CTRL_LTRIGGER)
					gTimeControl.scroll(-0.5f * speedModifier);
				if (pad.Buttons & PSP_CTRL_RTRIGGER)
					gTimeControl.scroll(0.5f * speedModifier);
			}
			oldPad = pad;
		}


		float d = gTimeControl.getDiscontinuity();
		if (d != 0.f)
		{
			streamWaveNudge((int)(d /*seconds*/ * 44100 /*samples per sec*/ * 4/*bytes per sample*/ * 4));
		}
//;;loopTime.peek();

		{
//;;updateTime.peek();
			//gDemo->processJobQueue();
			gDemo->updateFrame(gTimeControl.update(getDeltaTime()));
//;;updateTime.peek();

//;;finishAndSyncTime.peek();
			sceGuSync(0,0);
//;;finishAndSyncTime.peek();

;;static mutalisk::TimeBlock frameTime; frameTime.peek();

			if(doPrintInfo)
			{
				pspDebugScreenSetOffset((int)mainRenderTarget.vramAddr);
				pspDebugScreenSetXY(0,0);
				pspDebugScreenPrintf("mspf(%f)", frameTime.ms());
				pspDebugScreenPrintf("\tmem footprint = %i\t in use = %i", dlmalloc_footprint(), dlmalloc_inuse());
				//pspDebugScreenPrintf("timers: frame(%f) loop(%f) guFinish(%f)", frameTime.ms(), loopTime.ms(), finishAndSyncTime.ms());
				//pspDebugScreenPrintf("\n");
				//pspDebugScreenPrintf("mutalisk: update(%f) render(%f) sceneTime(%f)", updateTime.ms(), renderTime.ms(), gTimeControl.time());
				//pspDebugScreenPrintf("\n");
				//pspDebugScreenPrintf("allocated memory = %i", allocated_memory);
			}

			sceDisplayWaitVblankStart();
			mainRenderTarget2.vramAddr = mainRenderTarget.vramAddr;
			mainRenderTarget.vramAddr = sceGuSwapBuffers();

			saveAnimFrame(mainRenderTarget2.vramAddr);
			scePowerTick(0);
		}
//;;loopTime.peek();

		static int listId = 0;
		listId = 1-listId;
		sceGuStart(GU_DIRECT,list[listId]);

		//if(0)
		{

			setRenderTarget(mainRenderTarget);

			// clear screen

			sceGuClearColor(0xff000000);
			sceGuClearDepth(0xffff);
			sceGuClear(GU_COLOR_BUFFER_BIT|GU_DEPTH_BUFFER_BIT);

			sceGuAmbient(0x00101010);
			sceGuColor(0xffffff);

			ScePspFMatrix4 projMatrix;
			gumLoadIdentity(&projMatrix);

			float fovy = 45.0f;
			float aspect = 16.0f/9.0f;
			float zn = 1.0f;
			float zf = 50.0f;
			{
				ScePspFMatrix4 t;
				float angle = (fovy / 2) * (GU_PI/180.0f);
				float cotangent = cosf(angle) / sinf(angle);

				gumLoadIdentity(&t);

				t.x.x = cotangent / aspect;
				t.y.y = cotangent;
				t.z.z = zf/(zn-zf);// (far + near) / delta_z; // -(far + near) / delta_z
				t.w.z = zn*zf/(zn-zf);//2.0f * (far * near) / delta_z; // -2 * (far * near) / delta_z
				t.z.w = -1.0f;
				t.w.w = 0.0f;

				gumMultMatrix(&projMatrix,&projMatrix,&t);
			}

//;;renderTime.peek();
			gDemo->renderFrame();
//;;renderTime.peek();

			if(gDemo->ppSettings().strength > 0.0f && gDemo->ppSettings().srcModifier > 0)
			{
				bloom(mainRenderTarget, *renderTarget, *renderTarget2, 
					gDemo->ppSettings().strength, gDemo->ppSettings().threshold,
					gDemo->ppSettings().srcModifier, gDemo->ppSettings().dstModifier,
					gDemo->ppSettings().quality);
			}
		}

		sceGuFinish();
		val++;
	}

	sceGuTerm();

	sceKernelExitGame();
	return 0;
}

extern "C" 
{
int init_thread(SceSize args, void *argp)
{
	SceUID thid1 = pspSdkLoadStartModule("flash0:/kd/audiocodec.prx", PSP_MEMORY_PARTITION_KERNEL);
	SceUID thid2 = pspSdkLoadStartModule("flash0:/kd/libatrac3plus.prx", PSP_MEMORY_PARTITION_USER);
	while(!gStartMusic)
	{
		sceKernelDelayThread(8000);
	}
	if (thid1 < 0 || thid2 < 0)
	{
		pspDebugScreenPrintf("unable to load kernel modules\n");
		pspDebugScreenPrintf("thid1 = %x\n", thid1);
		pspDebugScreenPrintf("thid2 = %x\n", thid2);
	}
	else
	{
		std::string at3name = gPathPrefix + "music/suicidebarbie.at3";

		SceKernelLMOption option;
		{
			memset(&option, 0, sizeof(option));
			option.size = sizeof(option);
			option.mpidtext = PSP_MEMORY_PARTITION_USER;
			option.mpiddata = PSP_MEMORY_PARTITION_USER;
			option.position = 0;
			option.access = 1;
		}

		SceUID mod = sceKernelLoadModule((gPathPrefix + "music/atrac3streamer.prx").c_str(), 0, &option);
		if (mod >= 0)
		{
			int res;
			if (sceKernelStartModule(mod, at3name.size()+1, (void*)at3name.c_str(), &res, 0) < 0)
			{
				pspDebugScreenPrintf("Error starting module. \n");
			}
		}
		else
		{
			pspDebugScreenPrintf("Error opening module. \n");
		}
	}
	sceKernelExitDeleteThread(0);
	return 0;
}

__attribute__ ((constructor))
void loadKernelModules()
{
	SceUID initth = sceKernelCreateThread("init_thread", init_thread, 0x20, 0x10000, 0, NULL);

	if (initth >= 0)
		sceKernelStartThread(initth, 0, 0);	
}
}
