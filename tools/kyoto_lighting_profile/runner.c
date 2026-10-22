#include <signal.h>
#include <execinfo.h>
#include <unistd.h>
#include <mgba/core/blip_buf.h>
#include <mgba/flags.h>
#include <mgba/core/log.h>
#include <mgba/core/core.h>
#include <mgba/core/config.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/arm/arm.h>
#include <mgba-util/vfs.h>
#include <fcntl.h>
static void crash(int sig) {void *a[30]; int n=backtrace(a,30);backtrace_symbols_fd(a,n,2);_exit(1);}
static struct mCore *c;
static color_t pixels[240*160];
static void quiet(struct mLogger *l,int c,enum mLogLevel v,const char *f,va_list a) {}
static struct mLogger logger={quiet,0};
void start(const char *rom, const char *save) { signal(SIGSEGV,crash); mLogSetDefaultLogger(&logger); c=mCoreFind(rom); c->init(c); mCoreInitConfig(c, NULL); mCoreLoadFile(c,rom); if(save) c->loadTemporarySave(c,VFileOpen(save,O_RDONLY)); c->setVideoBuffer(c,pixels,240); c->reset(c); }
void frame(unsigned keys) { c->setKeys(c,keys); c->runFrame(c); blip_clear(c->getAudioChannel(c,0)); blip_clear(c->getAudioChannel(c,1)); }
unsigned read32(unsigned a) { return c->busRead32(c,a); }
unsigned read16(unsigned a) { return c->busRead16(c,a); }
unsigned read8(unsigned a) { return c->busRead8(c,a); }
void write32(unsigned a,unsigned v) { c->busWrite32(c,a,v); }
void write16(unsigned a,unsigned v) { c->busWrite16(c,a,v); }
void write8(unsigned a,unsigned v) { c->busWrite8(c,a,v); }
void *video(void) { return pixels; }
unsigned pc(void) { return ((struct ARMCore *)c->cpu)->gprs[15]; }
