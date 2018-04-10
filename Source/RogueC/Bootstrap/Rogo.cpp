#include <stdio.h>
namespace std {}
using namespace std;
#include "Rogo.h"

//=============================================================================
//  NativeCPP.cpp
//
//  Rogue runtime routines.
//=============================================================================

#include <fcntl.h>
#include <math.h>
#include <string.h>
#include <sys/timeb.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <inttypes.h>
#include <exception>
#include <cstddef>

#if !defined(ROGUE_PLATFORM_WINDOWS)
#  include <sys/time.h>
#  include <unistd.h>
#  include <signal.h>
#  include <dirent.h>
#  include <sys/socket.h>
#  include <sys/uio.h>
#  include <sys/stat.h>
#  include <netdb.h>
#  include <errno.h>
#endif

#if defined(ANDROID)
#  include <netinet/in.h>
#endif

#if defined(_WIN32)
#  include <direct.h>
#  define chdir _chdir
#endif

#if TARGET_OS_IPHONE
#  include <sys/types.h>
#  include <sys/sysctl.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef PATH_MAX
#  define PATH_MAX 4096
#endif

//-----------------------------------------------------------------------------
//  GLOBAL PROPERTIES
//-----------------------------------------------------------------------------
bool               Rogue_gc_logging   = false;
int                Rogue_gc_threshold = ROGUE_GC_THRESHOLD_DEFAULT;
int                Rogue_gc_count     = 0; // Purely informational
bool               Rogue_gc_requested = false;
bool               Rogue_gc_active    = false; // Are we collecting right now?
RogueLogical       Rogue_configured = 0;
int                Rogue_argc;
const char**       Rogue_argv;
RogueCallbackInfo  Rogue_on_gc_begin;
RogueCallbackInfo  Rogue_on_gc_trace_finished;
RogueCallbackInfo  Rogue_on_gc_end;
char               RogueDebugTrace::buffer[512];
ROGUE_THREAD_LOCAL RogueDebugTrace* Rogue_call_stack = 0;

struct RogueWeakReference;
RogueWeakReference* Rogue_weak_references = 0;

//-----------------------------------------------------------------------------
//  Multithreading
//-----------------------------------------------------------------------------
#if ROGUE_THREAD_MODE == ROGUE_THREAD_MODE_PTHREADS

#define ROGUE_MUTEX_LOCK(_M) pthread_mutex_lock(&(_M))
#define ROGUE_MUTEX_UNLOCK(_M) pthread_mutex_unlock(&(_M))
#define ROGUE_MUTEX_DEF(_N) pthread_mutex_t _N = PTHREAD_MUTEX_INITIALIZER

#define ROGUE_COND_STARTWAIT(_V,_M) ROGUE_MUTEX_LOCK(_M);
#define ROGUE_COND_DOWAIT(_V,_M,_C) while (_C) pthread_cond_wait(&(_V), &(_M));
#define ROGUE_COND_ENDWAIT(_V,_M) ROGUE_MUTEX_UNLOCK(_M);
#define ROGUE_COND_WAIT(_V,_M,_C) \
  ROGUE_COND_STARTWAIT(_V,_M); \
  ROGUE_COND_DOWAIT(_V,_M,_C); \
  ROGUE_COND_ENDWAIT(_V,_M);
#define ROGUE_COND_DEF(_N) pthread_cond_t _N = PTHREAD_COND_INITIALIZER
#define ROGUE_COND_NOTIFY_ONE(_V,_M,_C)    \
  ROGUE_MUTEX_LOCK(_M);                    \
  _C ;                                     \
  pthread_cond_signal(&(_V));              \
  ROGUE_MUTEX_UNLOCK(_M);
#define ROGUE_COND_NOTIFY_ALL(_V,_M,_C)    \
  ROGUE_MUTEX_LOCK(_M);                    \
  _C ;                                     \
  pthread_cond_broadcast(&(_V));           \
  ROGUE_MUTEX_UNLOCK(_M);

#define ROGUE_THREAD_DEF(_N) pthread_t _N
#define ROGUE_THREAD_JOIN(_T) pthread_join(_T, NULL)
#define ROGUE_THREAD_START(_T, _F) pthread_create(&(_T), NULL, _F, NULL)

#elif ROGUE_THREAD_MODE == ROGUE_THREAD_MODE_CPP

#include <exception>
#include <condition_variable>

#define ROGUE_MUTEX_LOCK(_M) _M.lock()
#define ROGUE_MUTEX_UNLOCK(_M) _M.unlock()
#define ROGUE_MUTEX_DEF(_N) std::mutex _N

#define ROGUE_COND_STARTWAIT(_V,_M) { std::unique_lock<std::mutex> LK(_M);
#define ROGUE_COND_DOWAIT(_V,_M,_C) while (_C) (_V).wait(LK);
#define ROGUE_COND_ENDWAIT(_V,_M) }
#define ROGUE_COND_WAIT(_V,_M,_C) \
  ROGUE_COND_STARTWAIT(_V,_M); \
  ROGUE_COND_DOWAIT(_V,_M,_C); \
  ROGUE_COND_ENDWAIT(_V,_M);
#define ROGUE_COND_DEF(_N) std::condition_variable _N
#define ROGUE_COND_NOTIFY_ONE(_V,_M,_C) {  \
  std::unique_lock<std::mutex> LK2(_M);    \
  _C ;                                     \
  (_V).notify_one(); }
#define ROGUE_COND_NOTIFY_ALL(_V,_M,_C) {  \
  std::unique_lock<std::mutex> LK2(_M);    \
  _C ;                                     \
  (_V).notify_all(); }

#define ROGUE_THREAD_DEF(_N) std::thread _N
#define ROGUE_THREAD_JOIN(_T) (_T).join()
#define ROGUE_THREAD_START(_T, _F) (_T = std::thread([] () {_F(NULL);}),0)

#endif

#if ROGUE_THREAD_MODE != ROGUE_THREAD_MODE_NONE

// Thread mutex locks around creation and destruction of threads
static ROGUE_MUTEX_DEF(Rogue_mt_thread_mutex);
static int Rogue_mt_tc = 0; // Thread count.  Always set under above lock.
static std::atomic_bool Rogue_mt_terminating(false); // True when terminating.

static void Rogue_thread_register ()
{
  ROGUE_MUTEX_LOCK(Rogue_mt_thread_mutex);
#if ROGUE_THREAD_MODE == ROGUE_THREAD_MODE_PTHREADS
  int n = (int)Rogue_mt_tc;
#endif
  ++Rogue_mt_tc;
  ROGUE_MUTEX_UNLOCK(Rogue_mt_thread_mutex);
#if ROGUE_THREAD_MODE == ROGUE_THREAD_MODE_PTHREADS
  char name[64];
  sprintf(name, "Thread-%i", n); // Nice names are good for valgrind

#if ROGUE_PLATFORM_MACOS
  pthread_setname_np(name);
#elif __linux__
  pthread_setname_np(pthread_self(), name);
#endif
// It should be possible to get thread names working on lots of other
// platforms too.  The functions just vary a bit.
#endif
}

static void Rogue_thread_unregister ()
{
  ROGUE_EXIT;
  ROGUE_MUTEX_LOCK(Rogue_mt_thread_mutex);
  ROGUE_ENTER;
  --Rogue_mt_tc;
  ROGUE_MUTEX_UNLOCK(Rogue_mt_thread_mutex);
}


#define ROGUE_THREADS_WAIT_FOR_ALL Rogue_threads_wait_for_all();

void Rogue_threads_wait_for_all ()
{
  Rogue_mt_terminating = true;
  ROGUE_EXIT;
  int wait = 2; // Initial Xms
  int wait_step = 1;
  while (true)
  {
    ROGUE_MUTEX_LOCK(Rogue_mt_thread_mutex);
    if (Rogue_mt_tc <= 1) // Shouldn't ever really be less than 1
    {
      ROGUE_MUTEX_UNLOCK(Rogue_mt_thread_mutex);
      break;
    }
    ROGUE_MUTEX_UNLOCK(Rogue_mt_thread_mutex);
    usleep(1000 * wait);
    wait_step++;
    if (!(wait_step % 15) && (wait < 500)) wait *= 2; // Max backoff ~500ms
  }
  ROGUE_ENTER;
}

#else

#define ROGUE_THREADS_WAIT_FOR_ALL /* no-op if there's only one thread! */

static void Rogue_thread_register ()
{
}
static void Rogue_thread_unregister ()
{
}

#endif

// Singleton handling
#if ROGUE_THREAD_MODE
#define ROGUE_GET_SINGLETON(_S) (_S)->_singleton.load()
#define ROGUE_SET_SINGLETON(_S,_V) (_S)->_singleton.store(_V);
#if ROGUE_THREAD_MODE == ROGUE_THREAD_MODE_PTHREADS
pthread_mutex_t Rogue_thread_singleton_lock;
#define ROGUE_SINGLETON_LOCK ROGUE_MUTEX_LOCK(Rogue_thread_singleton_lock);
#define ROGUE_SINGLETON_UNLOCK ROGUE_MUTEX_UNLOCK(Rogue_thread_singleton_lock);
#else
std::recursive_mutex Rogue_thread_singleton_lock;
#define ROGUE_SINGLETON_LOCK Rogue_thread_singleton_lock.lock();
#define ROGUE_SINGLETON_UNLOCK Rogue_thread_singleton_lock.unlock();
#endif
#else
#define ROGUE_GET_SINGLETON(_S) (_S)->_singleton
#define ROGUE_SET_SINGLETON(_S,_V) (_S)->_singleton = _V;
#define ROGUE_SINGLETON_LOCK
#define ROGUE_SINGLETON_UNLOCK
#endif

//-----------------------------------------------------------------------------
//  GC
//-----------------------------------------------------------------------------
#if ROGUE_GC_MODE_AUTO_MT
// See the Rogue MT GC diagram for an explanation of some of this.

#define ROGUE_GC_VAR static volatile int
// (Curiously, volatile seems to help performance slightly.)

static thread_local bool Rogue_mtgc_is_gc_thread = false;

#define ROGUE_MTGC_BARRIER asm volatile("" : : : "memory");

// Atomic LL insertion
#define ROGUE_LINKED_LIST_INSERT(__OLD,__NEW,__NEW_NEXT)            \
  for(;;) {                                                         \
    auto tmp = __OLD;                                               \
    __NEW_NEXT = tmp;                                               \
    if (__sync_bool_compare_and_swap(&(__OLD), tmp, __NEW)) break;  \
  }

// We assume malloc is safe, but the SOA needs safety if it's being used.
#if ROGUEMM_SMALL_ALLOCATION_SIZE_LIMIT >= 0
static ROGUE_MUTEX_DEF(Rogue_mtgc_soa_mutex);
#define ROGUE_GC_SOA_LOCK    ROGUE_MUTEX_LOCK(Rogue_mtgc_soa_mutex);
#define ROGUE_GC_SOA_UNLOCK  ROGUE_MUTEX_UNLOCK(Rogue_mtgc_soa_mutex);
#else
#define ROGUE_GC_SOA_LOCK
#define ROGUE_GC_SOA_UNLOCK
#endif

static inline void Rogue_collect_garbage_real ();
void Rogue_collect_garbage_real_noinline ()
{
  Rogue_collect_garbage_real();
}

#if ROGUE_THREAD_MODE
#if ROGUE_THREAD_MODE_PTHREADS
#elif ROGUE_THREAD_MODE_CPP
#else
#error Currently, only --threads=pthreads and --threads=cpp are supported with --gc=auto-mt
#endif
#endif

// This is how unlikely() works in the Linux kernel
#define ROGUE_UNLIKELY(_X) __builtin_expect(!!(_X), 0)

#define ROGUE_GC_CHECK if (ROGUE_UNLIKELY(Rogue_mtgc_w) \
  && !ROGUE_UNLIKELY(Rogue_mtgc_is_gc_thread))          \
  Rogue_mtgc_W2_W3_W4(); // W1

 ROGUE_MUTEX_DEF(Rogue_mtgc_w_mutex);
static ROGUE_MUTEX_DEF(Rogue_mtgc_s_mutex);
static ROGUE_COND_DEF(Rogue_mtgc_w_cond);
static ROGUE_COND_DEF(Rogue_mtgc_s_cond);

ROGUE_GC_VAR Rogue_mtgc_w = 0;
ROGUE_GC_VAR Rogue_mtgc_s = 0;

// Only one worker can be "running" (waiting for) the GC at a time.
// To run, set r = 1, and wait for GC to set it to 0.  If r is already
// 1, just wait.
static ROGUE_MUTEX_DEF(Rogue_mtgc_r_mutex);
static ROGUE_COND_DEF(Rogue_mtgc_r_cond);
ROGUE_GC_VAR Rogue_mtgc_r = 0;

static ROGUE_MUTEX_DEF(Rogue_mtgc_g_mutex);
static ROGUE_COND_DEF(Rogue_mtgc_g_cond);
ROGUE_GC_VAR Rogue_mtgc_g = 0; // Should GC

static int Rogue_mtgc_should_quit = 0; // 0:normal 1:should-quit 2:has-quit

static ROGUE_THREAD_DEF(Rogue_mtgc_thread);

static void Rogue_mtgc_W2_W3_W4 (void);
static inline void Rogue_mtgc_W3_W4 (void);

inline void Rogue_mtgc_B1 ()
{
  ROGUE_COND_NOTIFY_ONE(Rogue_mtgc_s_cond, Rogue_mtgc_s_mutex, ++Rogue_mtgc_s);
}

static inline void Rogue_mtgc_B2_etc ()
{
  Rogue_mtgc_W3_W4();
  // We can probably just do GC_CHECK here rather than this more expensive
  // locking version.
  ROGUE_MUTEX_LOCK(Rogue_mtgc_w_mutex);
  auto w = Rogue_mtgc_w;
  ROGUE_MUTEX_UNLOCK(Rogue_mtgc_w_mutex);
  if (ROGUE_UNLIKELY(w)) Rogue_mtgc_W2_W3_W4(); // W1
}

static inline void Rogue_mtgc_W3_W4 ()
{
  // W3
  ROGUE_COND_WAIT(Rogue_mtgc_w_cond, Rogue_mtgc_w_mutex, Rogue_mtgc_w != 0);

  // W4
  ROGUE_MUTEX_LOCK(Rogue_mtgc_s_mutex);
  --Rogue_mtgc_s;
  ROGUE_MUTEX_UNLOCK(Rogue_mtgc_s_mutex);
}

static void Rogue_mtgc_W2_W3_W4 ()
{
  // W2
  ROGUE_COND_NOTIFY_ONE(Rogue_mtgc_s_cond, Rogue_mtgc_s_mutex, ++Rogue_mtgc_s);
  Rogue_mtgc_W3_W4();
}


static thread_local int Rogue_mtgc_entered = 1;

inline void Rogue_mtgc_enter()
{
  if (ROGUE_UNLIKELY(Rogue_mtgc_is_gc_thread)) return;
  if (ROGUE_UNLIKELY(Rogue_mtgc_entered))
#ifdef ROGUE_MTGC_DEBUG
  {
    printf("ALREADY ENTERED\n");
    exit(1);
  }
#else
  {
    ++Rogue_mtgc_entered;
    return;
  }
#endif

  Rogue_mtgc_entered = 1;
  Rogue_mtgc_B2_etc();
}

inline void Rogue_mtgc_exit()
{
  if (ROGUE_UNLIKELY(Rogue_mtgc_is_gc_thread)) return;
  if (ROGUE_UNLIKELY(Rogue_mtgc_entered <= 0))
  {
    printf("Unabalanced Rogue enter/exit\n");
    exit(1);
  }

  --Rogue_mtgc_entered;
  Rogue_mtgc_B1();
}

static void Rogue_mtgc_M1_M2_GC_M3 (int quit)
{
  // M1
  ROGUE_MUTEX_LOCK(Rogue_mtgc_w_mutex);
  Rogue_mtgc_w = 1;
  ROGUE_MUTEX_UNLOCK(Rogue_mtgc_w_mutex);

  // M2
#if (ROGUE_THREAD_MODE == ROGUE_THREAD_MODE_PTHREADS) && ROGUE_MTGC_DEBUG
  ROGUE_MUTEX_LOCK(Rogue_mtgc_s_mutex);
  while (Rogue_mtgc_s != Rogue_mt_tc)
  {
    if (Rogue_mtgc_s > Rogue_mt_tc || Rogue_mtgc_s < 0)
    {
      printf("INVALID VALUE OF S %i %i\n", Rogue_mtgc_s, Rogue_mt_tc);
      exit(1);
    }

    pthread_cond_wait(&Rogue_mtgc_s_cond, &Rogue_mtgc_s_mutex);
  }
  // We should actually be okay holding the S lock until the
  // very end of the function if we want, and this would prevent
  // threads that were blocking from ever leaving B2.  But
  // We should be okay anyway, though S may temporarily != TC.
  //ROGUE_MUTEX_UNLOCK(Rogue_mtgc_s_mutex);
#else
  ROGUE_COND_STARTWAIT(Rogue_mtgc_s_cond, Rogue_mtgc_s_mutex);
  ROGUE_COND_DOWAIT(Rogue_mtgc_s_cond, Rogue_mtgc_s_mutex, Rogue_mtgc_s != Rogue_mt_tc);
#endif

#if ROGUE_MTGC_DEBUG
  ROGUE_MUTEX_LOCK(Rogue_mtgc_w_mutex);
  Rogue_mtgc_w = 2;
  ROGUE_MUTEX_UNLOCK(Rogue_mtgc_w_mutex);
#endif

  // GC
  // Grab the SOA lock for symmetry.  It should actually never
  // be held by another thread since they're all in GC sleep.
  ROGUE_GC_SOA_LOCK;
  Rogue_collect_garbage_real();

  //NOTE: It's possible (Rogue_mtgc_s != Rogue_mt_tc) here, if we gave up the S
  //      lock, though they should quickly go back to equality.

  if (quit)
  {
    // Run a few more times to finish up
    Rogue_collect_garbage_real_noinline();
    Rogue_collect_garbage_real_noinline();

    // Free from the SOA
    RogueAllocator_free_all();
  }
  ROGUE_GC_SOA_UNLOCK;

  // M3
  ROGUE_COND_NOTIFY_ALL(Rogue_mtgc_w_cond, Rogue_mtgc_w_mutex, Rogue_mtgc_w = 0);

  // Could have done this much earlier
  ROGUE_COND_ENDWAIT(Rogue_mtgc_s_cond, Rogue_mtgc_s_mutex);
}

static void * Rogue_mtgc_threadproc (void *)
{
  Rogue_mtgc_is_gc_thread = true;
  int quit = 0;
  while (quit == 0)
  {
    ROGUE_COND_STARTWAIT(Rogue_mtgc_g_cond, Rogue_mtgc_g_mutex);
    ROGUE_COND_DOWAIT(Rogue_mtgc_g_cond, Rogue_mtgc_g_mutex, !Rogue_mtgc_g && !Rogue_mtgc_should_quit);
    Rogue_mtgc_g = 0;
    quit = Rogue_mtgc_should_quit;
    ROGUE_COND_ENDWAIT(Rogue_mtgc_g_cond, Rogue_mtgc_g_mutex);

    ROGUE_MUTEX_LOCK(Rogue_mt_thread_mutex);

    Rogue_mtgc_M1_M2_GC_M3(quit);

    ROGUE_MUTEX_UNLOCK(Rogue_mt_thread_mutex);

    ROGUE_COND_NOTIFY_ALL(Rogue_mtgc_r_cond, Rogue_mtgc_r_mutex, Rogue_mtgc_r = 0);
  }

  ROGUE_MUTEX_LOCK(Rogue_mtgc_g_mutex);
  Rogue_mtgc_should_quit = 2;
  Rogue_mtgc_g = 0;
  ROGUE_MUTEX_UNLOCK(Rogue_mtgc_g_mutex);
  return NULL;
}

// Cause GC to run and wait for a GC to complete.
void Rogue_mtgc_run_gc_and_wait ()
{
  bool again;
  do
  {
    again = false;
    ROGUE_COND_STARTWAIT(Rogue_mtgc_r_cond, Rogue_mtgc_r_mutex);
    if (Rogue_mtgc_r == 0)
    {
      Rogue_mtgc_r = 1;

      // Signal GC to run
      ROGUE_COND_NOTIFY_ONE(Rogue_mtgc_g_cond, Rogue_mtgc_g_mutex, Rogue_mtgc_g = 1);
    }
    else
    {
      // If one or more simultaneous requests to run the GC came in, run it
      // again.
      again = (Rogue_mtgc_r == 1);
      ++Rogue_mtgc_r;
    }
    ROGUE_EXIT;
    ROGUE_COND_DOWAIT(Rogue_mtgc_r_cond, Rogue_mtgc_r_mutex, Rogue_mtgc_r != 0);
    ROGUE_COND_ENDWAIT(Rogue_mtgc_r_cond, Rogue_mtgc_r_mutex);
    ROGUE_ENTER;
  }
  while (again);
}

static void Rogue_mtgc_quit_gc_thread ()
{
  //NOTE: This could probably be simplified (and the quit behavior removed
  //      from Rogue_mtgc_M1_M2_GC_M3) since we now wait for all threads
  //      to stop before calling this.
  // This doesn't quite use the normal condition variable pattern, sadly.
  ROGUE_EXIT;
  timespec ts;
  ts.tv_sec = 0;
  ts.tv_nsec = 1000000 * 10; // 10ms
  while (true)
  {
    bool done = true;
    ROGUE_COND_STARTWAIT(Rogue_mtgc_g_cond, Rogue_mtgc_g_mutex);
    if (Rogue_mtgc_should_quit != 2)
    {
      done = false;
      Rogue_mtgc_g = 1;
      Rogue_mtgc_should_quit = 1;
#if ROGUE_THREAD_MODE == ROGUE_THREAD_MODE_PTHREADS
      pthread_cond_signal(&Rogue_mtgc_g_cond);
#elif ROGUE_THREAD_MODE == ROGUE_THREAD_MODE_CPP
      Rogue_mtgc_g_cond.notify_one();
#endif
    }
    ROGUE_COND_ENDWAIT(Rogue_mtgc_g_cond, Rogue_mtgc_g_mutex);
    if (done) break;
    nanosleep(&ts, NULL);
  }
  ROGUE_THREAD_JOIN(Rogue_mtgc_thread);
  ROGUE_ENTER;
}

void Rogue_configure_gc()
{
  int c = ROGUE_THREAD_START(Rogue_mtgc_thread, Rogue_mtgc_threadproc);
  if (c != 0)
  {
    exit(77); //TODO: Do something better in this (hopefully) rare case.
  }
}

// Used as part of the ROGUE_BLOCKING_CALL macro.
template<typename RT> RT Rogue_mtgc_reenter (RT expr)
{
  ROGUE_ENTER;
  return expr;
}

#include <atomic>

// We do all relaxed operations on this.  It's possible this will lead to
// something "bad", but I don't think *too* bad.  Like an extra GC.
// And I think that'll be rare, since the reset happens when all the
// threads are synced.  But I could be wrong.  Should probably think
// about this harder.
std::atomic_int Rogue_allocation_bytes_until_gc(Rogue_gc_threshold);
#define ROGUE_GC_COUNT_BYTES(__x) Rogue_allocation_bytes_until_gc.fetch_sub(__x, std::memory_order_relaxed);
#define ROGUE_GC_AT_THRESHOLD (Rogue_allocation_bytes_until_gc.load(std::memory_order_relaxed) <= 0)
#define ROGUE_GC_RESET_COUNT Rogue_allocation_bytes_until_gc.store(Rogue_gc_threshold, std::memory_order_relaxed);

#else // Anything besides auto-mt

#define ROGUE_GC_CHECK /* Does nothing in non-auto-mt modes */

#define ROGUE_GC_SOA_LOCK
#define ROGUE_GC_SOA_UNLOCK

int Rogue_allocation_bytes_until_gc = Rogue_gc_threshold;
#define ROGUE_GC_COUNT_BYTES(__x) Rogue_allocation_bytes_until_gc -= (__x);
#define ROGUE_GC_AT_THRESHOLD (Rogue_allocation_bytes_until_gc <= 0)
#define ROGUE_GC_RESET_COUNT Rogue_allocation_bytes_until_gc = Rogue_gc_threshold;


#define ROGUE_MTGC_BARRIER
#define ROGUE_LINKED_LIST_INSERT(__OLD,__NEW,__NEW_NEXT) do {__NEW_NEXT = __OLD; __OLD = __NEW;} while(false)

#endif

//-----------------------------------------------------------------------------
//  RogueDebugTrace
//-----------------------------------------------------------------------------
RogueDebugTrace::RogueDebugTrace( const char* method_signature, const char* filename, int line )
  : method_signature(method_signature), filename(filename), line(line), previous_trace(0)
{
  previous_trace = Rogue_call_stack;
  Rogue_call_stack = this;
}

RogueDebugTrace::~RogueDebugTrace()
{
  Rogue_call_stack = previous_trace;
}

int RogueDebugTrace::count()
{
  int n = 1;
  RogueDebugTrace* current = previous_trace;
  while (current)
  {
    ++n;
    current = current->previous_trace;
  }
  return n;
}

char* RogueDebugTrace::to_c_string()
{
  snprintf( buffer, 512, "[%s %s:%d]", method_signature, filename, line );
  return buffer;
}

//-----------------------------------------------------------------------------
//  RogueType
//-----------------------------------------------------------------------------
RogueArray* RogueType_create_array( int count, int element_size, bool is_reference_array, int element_type_index )
{
  if (count < 0) count = 0;
  int data_size  = count * element_size;
  int total_size = sizeof(RogueArray) + data_size;

  RogueArray* array = (RogueArray*) RogueAllocator_allocate_object( RogueTypeArray->allocator, RogueTypeArray, total_size, element_type_index);

  array->count = count;
  array->element_size = element_size;
  array->is_reference_array = is_reference_array;

  return array;
}

RogueObject* RogueType_create_object( RogueType* THIS, RogueInt32 size )
{
  ROGUE_DEF_LOCAL_REF_NULL(RogueObject*, obj);
  RogueInitFn  fn;
#if ROGUE_GC_MODE_BOEHM_TYPED
  ROGUE_DEBUG_STATEMENT(assert(size == 0 || size == THIS->object_size));
#endif

  obj = RogueAllocator_allocate_object( THIS->allocator, THIS, size ? size : THIS->object_size );

  if ((fn = THIS->init_object_fn)) return fn( obj );
  else                             return obj;
}

RogueLogical RogueType_instance_of( RogueType* THIS, RogueType* ancestor_type )
{
  if (THIS == ancestor_type)
  {
    return true;
  }

  int count = THIS->base_type_count;
  RogueType** base_type_ptr = THIS->base_types - 1;
  while (--count >= 0)
  {
    if (ancestor_type == *(++base_type_ptr))
    {
      return true;
    }
  }

  return false;
}

RogueString* RogueType_name( RogueType* THIS )
{
  return Rogue_literal_strings[ THIS->name_index ];
}

bool RogueType_name_equals( RogueType* THIS, const char* name )
{
  // For debugging purposes
  RogueString* st = Rogue_literal_strings[ THIS->name_index ];
  if ( !st ) return false;

  return (0 == strcmp((char*)st->utf8,name));
}

void RogueType_print_name( RogueType* THIS )
{
  RogueString* st = Rogue_literal_strings[ THIS->name_index ];
  if (st)
  {
    printf( "%s", st->utf8 );
  }
}

RogueType* RogueType_retire( RogueType* THIS )
{
  if (THIS->base_types)
  {
#if !ROGUE_GC_MODE_BOEHM
    delete [] THIS->base_types;
#endif
    THIS->base_types = 0;
    THIS->base_type_count = 0;
  }

  return THIS;
}

RogueObject* RogueType_singleton( RogueType* THIS )
{
  RogueInitFn fn;
  RogueObject * r = ROGUE_GET_SINGLETON(THIS);
  if (r) return r;

  ROGUE_SINGLETON_LOCK;

#if ROGUE_THREAD_MODE // Very minor optimization: don't check twice if unthreaded
  // We probably need to initialize the singleton, but now that we have the
  // lock, we double check.
  r = ROGUE_GET_SINGLETON(THIS);
  if (r)
  {
    // Ah, someone else just initialized it.  We'll use that.
    ROGUE_SINGLETON_UNLOCK;
  }
  else
#endif
  {
    // Yes, we'll be the one doing the initializing.

    // NOTE: _singleton must be assigned before calling init_object()
    // so we can't just call RogueType_create_object().
    r = RogueAllocator_allocate_object( THIS->allocator, THIS, THIS->object_size );

    if ((fn = THIS->init_object_fn)) r = fn( r );

    ROGUE_SET_SINGLETON(THIS, r);

    ROGUE_SINGLETON_UNLOCK;

    if ((fn = THIS->init_fn)) r = fn( THIS->_singleton );
  }

  return r;
}

//-----------------------------------------------------------------------------
//  RogueObject
//-----------------------------------------------------------------------------
RogueObject* RogueObject_as( RogueObject* THIS, RogueType* specialized_type )
{
  if (RogueObject_instance_of(THIS,specialized_type)) return THIS;
  return 0;
}

RogueLogical RogueObject_instance_of( RogueObject* THIS, RogueType* ancestor_type )
{
  if ( !THIS )
  {
    return false;
  }

  return RogueType_instance_of( THIS->type, ancestor_type );
}

void* RogueObject_retain( RogueObject* THIS )
{
  ROGUE_INCREF(THIS);
  return THIS;
}

void* RogueObject_release( RogueObject* THIS )
{
  ROGUE_DECREF(THIS);
  return THIS;
}

RogueString* RogueObject_to_string( RogueObject* THIS )
{
  RogueToStringFn fn = THIS->type->to_string_fn;
  if (fn) return fn( THIS );

  return Rogue_literal_strings[ THIS->type->name_index ];
}

void RogueObject_trace( void* obj )
{
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
}

void RogueString_trace( void* obj )
{
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
}

void RogueArray_trace( void* obj )
{
  int count;
  RogueObject** src;
  RogueArray* array = (RogueArray*) obj;

  if ( !array || array->object_size < 0 ) return;
  array->object_size = ~array->object_size;

  if ( !array->is_reference_array ) return;

  count = array->count;
  src = array->as_objects + count;
  while (--count >= 0)
  {
    RogueObject* cur = *(--src);
    if (cur && cur->object_size >= 0)
    {
      cur->type->trace_fn( cur );
    }
  }
}

//-----------------------------------------------------------------------------
//  RogueString
//-----------------------------------------------------------------------------
RogueString* RogueString_create_with_byte_count( int byte_count )
{
  if (byte_count < 0) byte_count = 0;

#if ROGUE_GC_MODE_BOEHM_TYPED
  RogueString* st = (RogueString*) RogueAllocator_allocate_object( RogueTypeString->allocator, RogueTypeString, RogueTypeString->object_size );
  char * data = (char *)GC_malloc_atomic_ignore_off_page( byte_count + 1 );
  data[0] = 0;
  data[byte_count] = 0;
  st->utf8 = (RogueByte*)data;
#else
  int total_size = sizeof(RogueString) + (byte_count+1);

  RogueString* st = (RogueString*) RogueAllocator_allocate_object( RogueTypeString->allocator, RogueTypeString, total_size );
#endif
  st->byte_count = byte_count;

  return st;
}

RogueString* RogueString_create_from_utf8( const char* utf8, int count )
{
  if (count == -1) count = (int) strlen( utf8 );

  RogueString* st = RogueString_create_with_byte_count( count );
  memcpy( st->utf8, utf8, count );
  return RogueString_validate( st );
}

RogueString* RogueString_create_from_characters( RogueCharacter_List* characters )
{
  if ( !characters ) return RogueString_create_with_byte_count(0);

  RogueCharacter* data = characters->data->as_characters;
  int count = characters->count;
  int utf8_count = 0;
  for (int i=count; --i>=0; )
  {
    RogueCharacter ch = data[i];
    if (ch <= 0x7F)         ++utf8_count;
    else if (ch <= 0x7FF)   utf8_count += 2;
    else if (ch <= 0xFFFF)  utf8_count += 3;
    else                    utf8_count += 4;
  }

  RogueString* result = RogueString_create_with_byte_count( utf8_count );
  char*   dest = result->utf8;
  for (int i=0; i<count; ++i)
  {
    RogueCharacter ch = data[i];
    if (ch < 0)
    {
      *(dest++) = 0;
    }
    else if (ch <= 0x7F)
    {
      *(dest++) = (RogueByte) ch;
    }
    else if (ch <= 0x7FF)
    {
      dest[0] = (RogueByte) (0xC0 | ((ch >> 6) & 0x1F));
      dest[1] = (RogueByte) (0x80 | (ch & 0x3F));
      dest += 2;
    }
    else if (ch <= 0xFFFF)
    {
      dest[0] = (RogueByte) (0xE0 | ((ch >> 12) & 0xF));
      dest[1] = (RogueByte) (0x80 | ((ch >> 6) & 0x3F));
      dest[2] = (RogueByte) (0x80 | (ch & 0x3F));
      dest += 3;
    }
    else
    {
      dest[0] = (RogueByte) (0xF0 | ((ch >> 18) & 0x7));
      dest[1] = (RogueByte) (0x80 | ((ch >> 12) & 0x3F));
      dest[2] = (RogueByte) (0x80 | ((ch >> 6) & 0x3F));
      dest[3] = (RogueByte) (0x80 | (ch & 0x3F));
      dest += 4;
    }
  }

  result->character_count = count;

  return RogueString_validate( result );
}

void RogueString_print_string( RogueString* st )
{
  if (st)
  {
    RogueString_print_utf8( st->utf8, st->byte_count );
  }
  else
  {
    printf( "null" );
  }
}

void RogueString_print_characters( RogueCharacter* characters, int count )
{
  if (characters)
  {
    RogueCharacter* src = characters - 1;
    while (--count >= 0)
    {
      int ch = *(++src);

      if (ch < 0)
      {
        putchar( 0 );
      }
      else if (ch < 0x80)
      {
        // %0xxxxxxx
        putchar( ch );
      }
      else if (ch < 0x800)
      {
        // %110xxxxx 10xxxxxx
        putchar( ((ch >> 6) & 0x1f) | 0xc0 );
        putchar( (ch & 0x3f) | 0x80 );
      }
      else if (ch <= 0xFFFF)
      {
        // %1110xxxx 10xxxxxx 10xxxxxx
        putchar( ((ch >> 12) & 15) | 0xe0 );
        putchar( ((ch >> 6) & 0x3f) | 0x80 );
        putchar( (ch & 0x3f) | 0x80 );
      }
      else
      {
        // Assume 21-bit
        // %11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
        putchar( 0xf0 | ((ch>>18) & 7) );
        putchar( 0x80 | ((ch>>12) & 0x3f) );
        putchar( 0x80 | ((ch>>6)  & 0x3f) );
        putchar( (ch & 0x3f) | 0x80 );
      }
    }
  }
  else
  {
    printf( "null" );
  }
}

void RogueString_print_utf8( char* utf8, int count )
{
  --utf8;
  while (--count >= 0)
  {
    putchar( *(++utf8) );
  }
}

RogueCharacter RogueString_character_at( RogueString* THIS, int index )
{
  if (THIS->is_ascii) return (RogueCharacter) THIS->utf8[ index ];

  RogueInt32 offset = RogueString_set_cursor( THIS, index );
  char* utf8 = THIS->utf8;

  RogueCharacter ch = utf8[ offset ];
  if (ch & 0x80)
  {
    if (ch & 0x20)
    {
      if (ch & 0x10)
      {
        return ((ch&7)<<18)
            | ((utf8[offset+1] & 0x3F) << 12)
            | ((utf8[offset+2] & 0x3F) << 6)
            | (utf8[offset+3] & 0x3F);
      }
      else
      {
        return ((ch&15)<<12)
            | ((utf8[offset+1] & 0x3F) << 6)
            | (utf8[offset+2] & 0x3F);
      }
    }
    else
    {
      return ((ch&31)<<6)
          | (utf8[offset+1] & 0x3F);
    }
  }
  else
  {
    return ch;
  }
}

RogueInt32 RogueString_set_cursor( RogueString* THIS, int index )
{
  // Sets this string's cursor_offset and cursor_index and returns cursor_offset.
  if (THIS->is_ascii)
  {
    return THIS->cursor_offset = THIS->cursor_index = index;
  }

  char* utf8 = THIS->utf8;

  RogueInt32 c_offset;
  RogueInt32 c_index;
  if (index == 0)
  {
    THIS->cursor_index = 0;
    return THIS->cursor_offset = 0;
  }
  else if (index >= THIS->character_count - 1)
  {
    c_offset = THIS->byte_count;
    c_index = THIS->character_count;
  }
  else
  {
    c_offset  = THIS->cursor_offset;
    c_index = THIS->cursor_index;
  }

  while (c_index < index)
  {
    while ((utf8[++c_offset] & 0xC0) == 0x80) {}
    ++c_index;
  }

  while (c_index > index)
  {
    while ((utf8[--c_offset] & 0xC0) == 0x80) {}
    --c_index;
  }

  THIS->cursor_index = c_index;
  return THIS->cursor_offset = c_offset;
}

RogueString* RogueString_validate( RogueString* THIS )
{
  // Trims any invalid UTF-8, counts the number of characters, and sets the hash code
  THIS->is_ascii = 1;  // assumption

  int character_count = 0;
  int byte_count = THIS->byte_count;
  int i;
  char* utf8 = THIS->utf8;
  for (i=0; i<byte_count; ++character_count)
  {
    int b = utf8[ i ];
    if (b & 0x80)
    {
      THIS->is_ascii = 0;
      if ( !(b & 0x40) ) { break;}  // invalid UTF-8

      if (b & 0x20)
      {
        if (b & 0x10)
        {
          // %11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
          if (b & 0x08) { break;}
          if (i + 4 > byte_count || ((utf8[i+1] & 0xC0) != 0x80) || ((utf8[i+2] & 0xC0) != 0x80)
              || ((utf8[i+3] & 0xC0) != 0x80)) { break;}
          i += 4;
        }
        else
        {
          // %1110xxxx 10xxxxxx 10xxxxxx
          if (i + 3 > byte_count || ((utf8[i+1] & 0xC0) != 0x80) || ((utf8[i+2] & 0xC0) != 0x80)) { break;}
          i += 3;
        }
      }
      else
      {
        // %110x xxxx 10xx xxxx
        if (i + 2 > byte_count || ((utf8[i+1] & 0xC0) != 0x80)) { break; }
        i += 2;
      }
    }
    else
    {
      ++i;
    }
  }

  if (i != byte_count)
  {
    printf( "*** RogueString validation error - invalid UTF8 (%d/%d):\n", i, byte_count );
    printf( "%02x\n", utf8[0] );
    printf( "%s\n", utf8 );
    utf8[ i ] = 0;
    Rogue_print_stack_trace();
  }

  THIS->byte_count = i;
  THIS->character_count = character_count;

  int code = 0;
  int len = THIS->byte_count;
  char* src = THIS->utf8 - 1;
  while (--len >= 0)
  {
    code = ((code<<3) - code) + *(++src);
  }
  THIS->hash_code = code;
  return THIS;
}

//-----------------------------------------------------------------------------
//  RogueArray
//-----------------------------------------------------------------------------
RogueArray* RogueArray_set( RogueArray* THIS, RogueInt32 dest_i1, RogueArray* src_array, RogueInt32 src_i1, RogueInt32 copy_count )
{
  int element_size;
  int dest_i2, src_i2;

  if ( !src_array || dest_i1 >= THIS->count ) return THIS;
  if (THIS->is_reference_array ^ src_array->is_reference_array) return THIS;

  if (copy_count == -1) src_i2 = src_array->count - 1;
  else                  src_i2 = (src_i1 + copy_count) - 1;

  if (dest_i1 < 0)
  {
    src_i1 -= dest_i1;
    dest_i1 = 0;
  }

  if (src_i1 < 0) src_i1 = 0;
  if (src_i2 >= src_array->count) src_i2 = src_array->count - 1;
  if (src_i1 > src_i2) return THIS;

  copy_count = (src_i2 - src_i1) + 1;
  dest_i2 = dest_i1 + (copy_count - 1);
  if (dest_i2 >= THIS->count)
  {
    dest_i2 = (THIS->count - 1);
    copy_count = (dest_i2 - dest_i1) + 1;
  }
  if ( !copy_count ) return THIS;


#if defined(ROGUE_ARC)
  if (THIS != src_array || dest_i1 >= src_i1 + copy_count || (src_i1 + copy_count) <= dest_i1 || dest_i1 < src_i1)
  {
    // no overlap
    RogueObject** src  = src_array->as_objects + src_i1 - 1;
    RogueObject** dest = THIS->as_objects + dest_i1 - 1;
    while (--copy_count >= 0)
    {
      RogueObject* src_obj, dest_obj;
      if ((src_obj = *(++src))) ROGUE_INCREF(src_obj);
      if ((dest_obj = *(++dest)) && !(ROGUE_DECREF(dest_obj)))
      {
        // TODO: delete dest_obj
        *dest = src_obj;
      }
    }
  }
  else
  {
    // Copying earlier data to later data; copy in reverse order to
    // avoid accidental overwriting
    if (dest_i1 > src_i1)  // if they're equal then we don't need to copy anything!
    {
      RogueObject** src  = src_array->as_objects + src_i2 + 1;
      RogueObject** dest = THIS->as_objects + dest_i2 + 1;
      while (--copy_count >= 0)
      {
        RogueObject* src_obj, dest_obj;
        if ((src_obj = *(--src))) ROGUE_INCREF(src_obj);
        if ((dest_obj = *(--dest)) && !(ROGUE_DECREF(dest_obj)))
        {
          // TODO: delete dest_obj
          *dest = src_obj;
        }
      }
    }
  }
  return THIS;
#endif

  element_size = THIS->element_size;
  RogueByte* src = src_array->as_bytes + src_i1 * element_size;
  RogueByte* dest = THIS->as_bytes + (dest_i1 * element_size);
  int copy_bytes = copy_count * element_size;

  if (src == dest) return THIS;

  if (src >= dest + copy_bytes || (src + copy_bytes) <= dest)
  {
    // Copy region does not overlap
    memcpy( dest, src, copy_count * element_size );
  }
  else
  {
    // Copy region overlaps
    memmove( dest, src, copy_count * element_size );
  }

  return THIS;
}

//-----------------------------------------------------------------------------
//  RogueAllocationPage
//-----------------------------------------------------------------------------
RogueAllocationPage* RogueAllocationPage_create( RogueAllocationPage* next_page )
{
  RogueAllocationPage* result = (RogueAllocationPage*) ROGUE_NEW_BYTES(sizeof(RogueAllocationPage));
  result->next_page = next_page;
  result->cursor = result->data;
  result->remaining = ROGUEMM_PAGE_SIZE;
  return result;
}

#if 0 // This is currently done statically.  Code likely to be removed.
RogueAllocationPage* RogueAllocationPage_delete( RogueAllocationPage* THIS )
{
  if (THIS) ROGUE_DEL_BYTES( THIS );
  return 0;
};
#endif

void* RogueAllocationPage_allocate( RogueAllocationPage* THIS, int size )
{
  // Round size up to multiple of 8.
  if (size > 0) size = (size + 7) & ~7;
  else          size = 8;

  if (size > THIS->remaining) return 0;

  //printf( "Allocating %d bytes from page.\n", size );
  void* result = THIS->cursor;
  THIS->cursor += size;
  THIS->remaining -= size;
  ((RogueObject*)result)->reference_count = 0;

  //printf( "%d / %d\n", ROGUEMM_PAGE_SIZE - remaining, ROGUEMM_PAGE_SIZE );
  return result;
}


//-----------------------------------------------------------------------------
//  RogueAllocator
//-----------------------------------------------------------------------------
#if 0 // This is currently done statically.  Code likely to be removed.
RogueAllocator* RogueAllocator_create()
{
  RogueAllocator* result = (RogueAllocator*) ROGUE_NEW_BYTES( sizeof(RogueAllocator) );

  memset( result, 0, sizeof(RogueAllocator) );

  return result;
}

RogueAllocator* RogueAllocator_delete( RogueAllocator* THIS )
{
  while (THIS->pages)
  {
    RogueAllocationPage* next_page = THIS->pages->next_page;
    RogueAllocationPage_delete( THIS->pages );
    THIS->pages = next_page;
  }
  return 0;
}
#endif

void* RogueAllocator_allocate( RogueAllocator* THIS, int size )
{
#if ROGUE_GC_MODE_AUTO_MT
#if ROGUE_MTGC_DEBUG
    ROGUE_MUTEX_LOCK(Rogue_mtgc_w_mutex);
    if (Rogue_mtgc_w == 2)
    {
      printf("ALLOC DURING GC!\n");
      exit(1);
    }
    ROGUE_MUTEX_UNLOCK(Rogue_mtgc_w_mutex);
#endif
#endif

#if ROGUE_GC_MODE_AUTO_ANY
  Rogue_collect_garbage();
#endif
  if (size > ROGUEMM_SMALL_ALLOCATION_SIZE_LIMIT)
  {
    ROGUE_GC_COUNT_BYTES(size);
    void * mem = ROGUE_NEW_BYTES(size);
#if ROGUE_GC_MODE_AUTO_ANY
    if (!mem)
    {
      // Try hard!
      Rogue_collect_garbage(true);
      mem = ROGUE_NEW_BYTES(size);
    }
#endif
    return mem;
  }

  ROGUE_GC_SOA_LOCK;

  size = (size > 0) ? (size + ROGUEMM_GRANULARITY_MASK) & ~ROGUEMM_GRANULARITY_MASK : ROGUEMM_GRANULARITY_SIZE;

  ROGUE_GC_COUNT_BYTES(size);

  int slot;
  ROGUE_DEF_LOCAL_REF(RogueObject*, obj, THIS->available_objects[(slot=(size>>ROGUEMM_GRANULARITY_BITS))]);

  if (obj)
  {
    //printf( "found free object\n");
    THIS->available_objects[slot] = obj->next_object;
    ROGUE_GC_SOA_UNLOCK;
    return obj;
  }

  // No free objects for requested size.

  // Try allocating a new object from the current page.
  if (THIS->pages )
  {
    obj = (RogueObject*) RogueAllocationPage_allocate( THIS->pages, size );
    if (obj)
    {
      ROGUE_GC_SOA_UNLOCK;
      return obj;
    }

    // Not enough room on allocation page.  Allocate any smaller blocks
    // we're able to and then move on to a new page.
    int s = slot - 1;
    while (s >= 1)
    {
      obj = (RogueObject*) RogueAllocationPage_allocate( THIS->pages, s << ROGUEMM_GRANULARITY_BITS );
      if (obj)
      {
        //printf( "free obj size %d\n", (s << ROGUEMM_GRANULARITY_BITS) );
        obj->next_object = THIS->available_objects[s];
        THIS->available_objects[s] = obj;
      }
      else
      {
        --s;
      }
    }
  }

  // New page; this will work for sure.
  THIS->pages = RogueAllocationPage_create( THIS->pages );
  void * r = RogueAllocationPage_allocate( THIS->pages, size );
  ROGUE_GC_SOA_UNLOCK;
  return r;
}

#if ROGUE_GC_MODE_BOEHM
void Rogue_Boehm_Finalizer( void* obj, void* data )
{
  RogueObject* o = (RogueObject*)obj;
  o->type->on_cleanup_fn(o);
}

RogueObject* RogueAllocator_allocate_object( RogueAllocator* THIS, RogueType* of_type, int size, int element_type_index )
{
  // We use the "off page" allocations here, which require that somewhere there's a pointer
  // to something within the first 256 bytes.  Since someone should always be holding a
  // reference to the absolute start of the allocation (a reference!), this should always
  // be true.
#if ROGUE_GC_MODE_BOEHM_TYPED
  RogueObject * obj;
  if (of_type->gc_alloc_type == ROGUE_GC_ALLOC_TYPE_TYPED)
  {
    obj = (RogueObject*)GC_malloc_explicitly_typed_ignore_off_page(of_type->object_size, of_type->gc_type_descr);
  }
  else if (of_type->gc_alloc_type == ROGUE_GC_ALLOC_TYPE_ATOMIC)
  {
    obj = (RogueObject*)GC_malloc_atomic_ignore_off_page( of_type->object_size );
    if (obj) memset( obj, 0, of_type->object_size );
  }
  else
  {
    obj = (RogueObject*)GC_malloc_ignore_off_page( of_type->object_size );
  }
  if (!obj)
  {
    Rogue_collect_garbage( true );
    obj = (RogueObject*)GC_MALLOC( of_type->object_size );
  }
  obj->object_size = of_type->object_size;
#else
  RogueObject * obj = (RogueObject*)GC_malloc_ignore_off_page( size );
  if (!obj)
  {
    Rogue_collect_garbage( true );
    obj = (RogueObject*)GC_MALLOC( size );
  }
  obj->object_size = size;
#endif

  ROGUE_GCDEBUG_STATEMENT( printf( "Allocating " ) );
  ROGUE_GCDEBUG_STATEMENT( RogueType_print_name(of_type) );
  ROGUE_GCDEBUG_STATEMENT( printf( " %p\n", (RogueObject*)obj ) );
  //ROGUE_GCDEBUG_STATEMENT( Rogue_print_stack_trace() );

#if ROGUE_GC_MODE_BOEHM_TYPED
  // In typed mode, we allocate the array object and the actual data independently so that
  // they can have different GC types.
  if (element_type_index != -1)
  {
    RogueType* el_type = &Rogue_types[element_type_index];
    int data_size = size - of_type->object_size;
    int elements = data_size / el_type->object_size;
    void * data;
    if (el_type->gc_alloc_type == ROGUE_GC_ALLOC_TYPE_TYPED)
    {
      data = GC_calloc_explicitly_typed(elements, el_type->object_size, el_type->gc_type_descr);
    }
    else if (of_type->gc_alloc_type == ROGUE_GC_ALLOC_TYPE_ATOMIC)
    {
      data = GC_malloc_atomic_ignore_off_page( data_size );
      if (data) memset( obj, 0, data_size );
    }
    else
    {
      data = GC_malloc_ignore_off_page( data_size );
    }
    ((RogueArray*)obj)->as_bytes = (RogueByte*)data;
    ROGUE_GCDEBUG_STATEMENT( printf( "Allocating " ) );
    ROGUE_GCDEBUG_STATEMENT( printf( "  Elements " ) );
    ROGUE_GCDEBUG_STATEMENT( RogueType_print_name(el_type) );
    ROGUE_GCDEBUG_STATEMENT( printf( " %p\n", (RogueObject*)data ) );
  }
#endif

  if (of_type->on_cleanup_fn)
  {
    GC_REGISTER_FINALIZER_IGNORE_SELF( obj, Rogue_Boehm_Finalizer, 0, 0, 0 );
  }

  obj->type = of_type;

  return obj;
}
#else
RogueObject* RogueAllocator_allocate_object( RogueAllocator* THIS, RogueType* of_type, int size, int element_type_index )
{
  void * mem = RogueAllocator_allocate( THIS, size );
  memset( mem, 0, size );

  ROGUE_DEF_LOCAL_REF(RogueObject*, obj, (RogueObject*)mem);

  ROGUE_GCDEBUG_STATEMENT( printf( "Allocating " ) );
  ROGUE_GCDEBUG_STATEMENT( RogueType_print_name(of_type) );
  ROGUE_GCDEBUG_STATEMENT( printf( " %p\n", (RogueObject*)obj ) );
  //ROGUE_GCDEBUG_STATEMENT( Rogue_print_stack_trace() );

  obj->type = of_type;
  obj->object_size = size;

  ROGUE_MTGC_BARRIER; // Probably not necessary

  if (of_type->on_cleanup_fn)
  {
    ROGUE_LINKED_LIST_INSERT(THIS->objects_requiring_cleanup, obj, obj->next_object);
  }
  else
  {
    ROGUE_LINKED_LIST_INSERT(THIS->objects, obj, obj->next_object);
  }

  return obj;
}
#endif

void* RogueAllocator_free( RogueAllocator* THIS, void* data, int size )
{
  if (data)
  {
    ROGUE_GCDEBUG_STATEMENT(memset(data,0,size));
    if (size > ROGUEMM_SMALL_ALLOCATION_SIZE_LIMIT)
    {
      // When debugging GC, it can be very useful to log the object types of
      // freed objects.  When valgrind points out access to freed memory, you
      // can then see what it was.
      #if 0
      RogueObject* obj = (RogueObject*) data;
      printf("DEL %i %p ", (int)pthread_self(), data);
      RogueType_print_name( obj-> type );
      printf("\n");
      #endif
      ROGUE_DEL_BYTES( data );
    }
    else
    {
      // Return object to small allocation pool
      RogueObject* obj = (RogueObject*) data;
      int slot = (size + ROGUEMM_GRANULARITY_MASK) >> ROGUEMM_GRANULARITY_BITS;
      if (slot <= 0) slot = 1;
      obj->next_object = THIS->available_objects[slot];
      THIS->available_objects[slot] = obj;
    }
  }

  // Always returns null, allowing a pointer to be freed and assigned null in
  // a single step.
  return 0;
}


void RogueAllocator_free_objects( RogueAllocator* THIS )
{
  RogueObject* objects = THIS->objects;
  while (objects)
  {
    RogueObject* next_object = objects->next_object;
    RogueAllocator_free( THIS, objects, objects->object_size );
    objects = next_object;
  }

  THIS->objects = 0;
}

void RogueAllocator_free_all( )
{
  for (int i=0; i<Rogue_allocator_count; ++i)
  {
    RogueAllocator_free_objects( &Rogue_allocators[i] );
  }
}

void RogueAllocator_collect_garbage( RogueAllocator* THIS )
{
  // Global program objects have already been traced through.

  // Trace through all as-yet unreferenced objects that are manually retained.
  RogueObject* cur = THIS->objects;
  while (cur)
  {
    if (cur->object_size >= 0 && cur->reference_count > 0)
    {
      cur->type->trace_fn( cur );
    }
    cur = cur->next_object;
  }

  cur = THIS->objects_requiring_cleanup;
  while (cur)
  {
    if (cur->object_size >= 0 && cur->reference_count > 0)
    {
      cur->type->trace_fn( cur );
    }
    cur = cur->next_object;
  }

  // For any unreferenced objects requiring clean-up, we'll:
  //   1.  Reference them and move them to a separate short-term list.
  //   2.  Finish the regular GC.
  //   3.  Call on_cleanup() on each of them, which may create new
  //       objects (which is why we have to wait until after the GC).
  //   4.  Move them to the list of regular objects.
  cur = THIS->objects_requiring_cleanup;
  RogueObject* unreferenced_on_cleanup_objects = 0;
  RogueObject* survivors = 0;  // local var for speed
  while (cur)
  {
    RogueObject* next_object = cur->next_object;
    if (cur->object_size < 0)
    {
      // Referenced.
      cur->object_size = ~cur->object_size;
      cur->next_object = survivors;
      survivors = cur;
    }
    else
    {
      // Unreferenced - go ahead and trace it since we'll call on_cleanup
      // on it.
      cur->type->trace_fn( cur );
      cur->next_object = unreferenced_on_cleanup_objects;
      unreferenced_on_cleanup_objects = cur;
    }
    cur = next_object;
  }
  THIS->objects_requiring_cleanup = survivors;

  // All objects are in a state where a non-negative size means that the object is
  // due to be deleted.
  Rogue_on_gc_trace_finished.call();

  // Reset or delete each general object
  cur = THIS->objects;
  THIS->objects = 0;
  survivors = 0;  // local var for speed

  while (cur)
  {
    RogueObject* next_object = cur->next_object;
    if (cur->object_size < 0)
    {
      cur->object_size = ~cur->object_size;
      cur->next_object = survivors;
      survivors = cur;
    }
    else
    {
      ROGUE_GCDEBUG_STATEMENT( printf( "Freeing " ) );
      ROGUE_GCDEBUG_STATEMENT( RogueType_print_name(cur->type) );
      ROGUE_GCDEBUG_STATEMENT( printf( " %p\n", cur ) );
      RogueAllocator_free( THIS, cur, cur->object_size );
    }
    cur = next_object;
  }

  THIS->objects = survivors;


  // Call on_cleanup() on unreferenced objects requiring cleanup
  // and move them to the general objects list so they'll be deleted
  // the next time they're unreferenced.  Calling on_cleanup() may
  // create additional objects so THIS->objects may change during a
  // on_cleanup() call.
  cur = unreferenced_on_cleanup_objects;
  while (cur)
  {
    RogueObject* next_object = cur->next_object;

    cur->type->on_cleanup_fn( cur );

    cur->object_size = ~cur->object_size;
    cur->next_object = THIS->objects;
    THIS->objects = cur;

    cur = next_object;
  }

  if (Rogue_gc_logging)
  {
    int byte_count = 0;
    int object_count = 0;

    for (int i=0; i<Rogue_allocator_count; ++i)
    {
      RogueAllocator* allocator = &Rogue_allocators[i];

      RogueObject* cur = allocator->objects;
      while (cur)
      {
        ++object_count;
        byte_count += cur->object_size;
        cur = cur->next_object;
      }

      cur = allocator->objects_requiring_cleanup;
      while (cur)
      {
        ++object_count;
        byte_count += cur->object_size;
        cur = cur->next_object;
      }
    }

    printf( "Post-GC: %d objects, %d bytes used.\n", object_count, byte_count );
  }
}

void Rogue_print_stack_trace ( bool leading_newline )
{
  RogueDebugTrace* current = Rogue_call_stack;
  if (current && leading_newline) printf( "\n" );
  while (current)
  {
    printf( "%s\n", current->to_c_string() );
    current = current->previous_trace;
  }
  printf("\n");
}

#if defined(ROGUE_PLATFORM_WINDOWS)
void Rogue_segfault_handler( int signal )
{
  printf( "Access violation\n" );
#else
void Rogue_segfault_handler( int signal, siginfo_t *si, void *arg )
{
  if (si->si_addr < (void*)4096)
  {
    // Probably a null pointer dereference.
    printf( "Null reference error (accessing memory at %p)\n",
            si->si_addr );
  }
  else
  {
    if (si->si_code == SEGV_MAPERR)
      printf( "Access to unmapped memory at " );
    else if (si->si_code == SEGV_ACCERR)
      printf( "Access to forbidden memory at " );
    else
      printf( "Unknown segfault accessing " );
    printf("%p\n", si->si_addr);
  }
#endif

  Rogue_print_stack_trace( true );

  exit(1);
}

void Rogue_update_weak_references_during_gc()
{
  RogueWeakReference* cur = Rogue_weak_references;
  while (cur)
  {
    if (cur->value && cur->value->object_size >= 0)
    {
      // The value held by this weak reference is about to be deleted by the
      // GC system; null out the value.
      cur->value = 0;
    }
    cur = cur->next_weak_reference;
  }
}


void Rogue_configure_types()
{
#if ROGUE_THREAD_MODE == ROGUE_THREAD_MODE_PTHREADS
_rogue_init_mutex(&Rogue_thread_singleton_lock);
#endif

  int i;
  const int* next_type_info = Rogue_type_info_table;

#if defined(ROGUE_PLATFORM_WINDOWS)
  // Use plain old signal() instead of sigaction()
  signal( SIGSEGV, Rogue_segfault_handler );
#else
  // Install seg fault handler
  struct sigaction sa;

  memset( &sa, 0, sizeof(sa) );
  sigemptyset( &sa.sa_mask );
  sa.sa_sigaction = Rogue_segfault_handler;
  sa.sa_flags     = SA_SIGINFO;

  sigaction( SIGSEGV, &sa, NULL );
#endif

  // Initialize allocators
  memset( Rogue_allocators, 0, sizeof(RogueAllocator)*Rogue_allocator_count );

#ifdef ROGUE_INTROSPECTION
  int global_property_pointer_cursor = 0;
  int property_offset_cursor = 0;
#endif

#ifdef ROGUE_OLD_TYPE_INFO
  const int* type_info = next_type_info;
#endif
  // Initialize types
  for (i=0; i<Rogue_type_count; ++i)
  {
    int j;
    RogueType* type = &Rogue_types[i];
#ifndef ROGUE_OLD_TYPE_INFO
    const int* type_info = next_type_info;
    next_type_info += *(type_info++) + 1;
#endif

    memset( type, 0, sizeof(RogueType) );

    type->index = i;
    type->name_index = Rogue_type_name_index_table[i];
    type->object_size = Rogue_object_size_table[i];
#ifdef ROGUE_INTROSPECTION
    type->attributes = Rogue_attributes_table[i];
#endif
    type->allocator = &Rogue_allocators[ *(type_info++) ];
    type->methods = Rogue_dynamic_method_table + *(type_info++);
    type->base_type_count = *(type_info++);
    if (type->base_type_count)
    {
#if ROGUE_GC_MODE_BOEHM
      type->base_types = new (NoGC) RogueType*[ type->base_type_count ];
#else
      type->base_types = new RogueType*[ type->base_type_count ];
#endif
      for (j=0; j<type->base_type_count; ++j)
      {
        type->base_types[j] = &Rogue_types[ *(type_info++) ];
      }
    }

    type->global_property_count = *(type_info++);
    type->global_property_name_indices = type_info;
    type_info += type->global_property_count;
    type->global_property_type_indices = type_info;
    type_info += type->global_property_count;

    type->property_count = *(type_info++);
    type->property_name_indices = type_info;
    type_info += type->property_count;
    type->property_type_indices = type_info;
    type_info += type->property_count;

#ifdef ROGUE_INTROSPECTION
    if (((type->attributes & ROGUE_ATTRIBUTE_TYPE_MASK) == ROGUE_ATTRIBUTE_IS_CLASS)
      || ((type->attributes & ROGUE_ATTRIBUTE_TYPE_MASK) == ROGUE_ATTRIBUTE_IS_COMPOUND))
    {
      type->global_property_pointers = Rogue_global_property_pointers + global_property_pointer_cursor;
      global_property_pointer_cursor += type->global_property_count;
      type->property_offsets = Rogue_property_offsets + property_offset_cursor;
      property_offset_cursor += type->property_count;
    }
    if (((type->attributes & ROGUE_ATTRIBUTE_TYPE_MASK) == ROGUE_ATTRIBUTE_IS_CLASS)
      || ((type->attributes & ROGUE_ATTRIBUTE_TYPE_MASK) == ROGUE_ATTRIBUTE_IS_COMPOUND)
      || ((type->attributes & ROGUE_ATTRIBUTE_TYPE_MASK) == ROGUE_ATTRIBUTE_IS_ASPECT))
    {
      type->method_count = *(type_info++);
    }
#endif

    type->trace_fn = Rogue_trace_fn_table[i];
    type->init_object_fn = Rogue_init_object_fn_table[i];
    type->init_fn        = Rogue_init_fn_table[i];
    type->on_cleanup_fn  = Rogue_on_cleanup_fn_table[i];
    type->to_string_fn   = Rogue_to_string_fn_table[i];

#ifndef ROGUE_OLD_TYPE_INFO
    ROGUE_DEBUG_STATEMENT(assert(type_info <= next_type_info));
#endif
  }

  Rogue_on_gc_trace_finished.add( Rogue_update_weak_references_during_gc );

#if ROGUE_GC_MODE_BOEHM_TYPED
  Rogue_init_boehm_type_info();
#endif
}

#if ROGUE_GC_MODE_BOEHM
static GC_ToggleRefStatus Rogue_Boehm_ToggleRefStatus( void * o )
{
  RogueObject* obj = (RogueObject*)o;
  if (obj->reference_count > 0) return GC_TOGGLE_REF_STRONG;
  return GC_TOGGLE_REF_DROP;
}

static void Rogue_Boehm_on_collection_event( GC_EventType event )
{
  if (event == GC_EVENT_START)
  {
    Rogue_on_gc_begin.call();
  }
  else if (event == GC_EVENT_END)
  {
    Rogue_on_gc_end.call();
  }
}

void Rogue_configure_gc()
{
  // Initialize Boehm collector
  //GC_set_finalize_on_demand(0);
  GC_set_toggleref_func(Rogue_Boehm_ToggleRefStatus);
  GC_set_on_collection_event(Rogue_Boehm_on_collection_event);
  //GC_set_all_interior_pointers(0);
  GC_INIT();
}
#elif ROGUE_GC_MODE_AUTO_MT
// Rogue_configure_gc already defined above.
#else
void Rogue_configure_gc()
{
}
#endif

#if ROGUE_GC_MODE_BOEHM
bool Rogue_collect_garbage( bool forced )
{
  if (forced)
  {
    GC_gcollect();
    return true;
  }

  return GC_collect_a_little();
}
#else // Auto or manual

static inline void Rogue_collect_garbage_real(void);

bool Rogue_collect_garbage( bool forced )
{
  if (!forced && !Rogue_gc_requested & !ROGUE_GC_AT_THRESHOLD) return false;

#if ROGUE_GC_MODE_AUTO_MT
  Rogue_mtgc_run_gc_and_wait();
#else
  Rogue_collect_garbage_real();
#endif

  return true;
}

static inline void Rogue_collect_garbage_real()
{
  Rogue_gc_requested = false;
  if (Rogue_gc_active) return;
  Rogue_gc_active = true;
  ++ Rogue_gc_count;

//printf( "GC %d\n", Rogue_allocation_bytes_until_gc );
  ROGUE_GC_RESET_COUNT;

  Rogue_on_gc_begin.call();

  Rogue_trace();

  for (int i=0; i<Rogue_allocator_count; ++i)
  {
    RogueAllocator_collect_garbage( &Rogue_allocators[i] );
  }

  Rogue_on_gc_end.call();
  Rogue_gc_active = false;
}

#endif

void Rogue_quit()
{
  int i;

  if ( !Rogue_configured ) return;
  Rogue_configured = 0;

  RogueGlobal__call_exit_functions( (RogueClassGlobal*) ROGUE_SINGLETON(Global) );

  ROGUE_THREADS_WAIT_FOR_ALL;

#if ROGUE_GC_MODE_AUTO_MT
  Rogue_mtgc_quit_gc_thread();
#else
  // Give a few GC's to allow objects requiring clean-up to do so.
  Rogue_collect_garbage( true );
  Rogue_collect_garbage( true );
  Rogue_collect_garbage( true );

  RogueAllocator_free_all();
#endif

  for (i=0; i<Rogue_type_count; ++i)
  {
    RogueType_retire( &Rogue_types[i] );
  }

  Rogue_thread_unregister();
}

#if ROGUE_GC_MODE_BOEHM

void Rogue_Boehm_IncRef (RogueObject* o)
{
  ++o->reference_count;
  if (o->reference_count == 1)
  {
    GC_toggleref_add(o, 1);
  }
}
void Rogue_Boehm_DecRef (RogueObject* o)
{
  --o->reference_count;
  if (o->reference_count < 0)
  {
    o->reference_count = 0;
  }
}
#endif


//-----------------------------------------------------------------------------
//  Exception handling
//-----------------------------------------------------------------------------
void Rogue_terminate_handler()
{
  printf( "Uncaught exception.\n" );
  exit(1);
}
//=============================================================================
typedef void(*ROGUEM0)(void*);
typedef void*(*ROGUEM1)(void*);
typedef RogueLogical(*ROGUEM2)(void*,void*);
typedef RogueInt32(*ROGUEM3)(void*);
typedef void*(*ROGUEM4)(void*,void*);
typedef void*(*ROGUEM5)(void*,RogueInt32);
typedef RogueLogical(*ROGUEM6)(void*);
typedef void*(*ROGUEM7)(void*,void*,void*);
typedef RogueInt64(*ROGUEM8)(void*);
typedef RogueReal64(*ROGUEM9)(void*);
typedef void*(*ROGUEM10)(void*,void*,RogueInt32);
typedef RogueLogical(*ROGUEM11)(void*,void*,void*);

// (Function()).call()
extern void Rogue_call_ROGUEM0( int i, void* THIS )
{
  ((ROGUEM0)(((RogueObject*)THIS)->type->methods[i]))( THIS );
}

// Object.init()
extern void* Rogue_call_ROGUEM1( int i, void* THIS )
{
  return ((ROGUEM1)(((RogueObject*)THIS)->type->methods[i]))( THIS );
}

// Value.operator==(Value)
extern RogueLogical Rogue_call_ROGUEM2( int i, void* THIS, void* p0 )
{
  return ((ROGUEM2)(((RogueObject*)THIS)->type->methods[i]))( THIS, p0 );
}

// Value.to_Int32()
extern RogueInt32 Rogue_call_ROGUEM3( int i, void* THIS )
{
  return ((ROGUEM3)(((RogueObject*)THIS)->type->methods[i]))( THIS );
}

// Value.remove(Value)
extern void* Rogue_call_ROGUEM4( int i, void* THIS, void* p0 )
{
  return ((ROGUEM4)(((RogueObject*)THIS)->type->methods[i]))( THIS, p0 );
}

// Value.get(Int32)
extern void* Rogue_call_ROGUEM5( int i, void* THIS, RogueInt32 p0 )
{
  return ((ROGUEM5)(((RogueObject*)THIS)->type->methods[i]))( THIS, p0 );
}

// Value.is_logical()
extern RogueLogical Rogue_call_ROGUEM6( int i, void* THIS )
{
  return ((ROGUEM6)(((RogueObject*)THIS)->type->methods[i]))( THIS );
}

// Value.set(String,Value)
extern void* Rogue_call_ROGUEM7( int i, void* THIS, void* p0, void* p1 )
{
  return ((ROGUEM7)(((RogueObject*)THIS)->type->methods[i]))( THIS, p0, p1 );
}

// Value.to_Int64()
extern RogueInt64 Rogue_call_ROGUEM8( int i, void* THIS )
{
  return ((ROGUEM8)(((RogueObject*)THIS)->type->methods[i]))( THIS );
}

// Value.to_Real64()
extern RogueReal64 Rogue_call_ROGUEM9( int i, void* THIS )
{
  return ((ROGUEM9)(((RogueObject*)THIS)->type->methods[i]))( THIS );
}

// Value.to_json(StringBuilder,Int32)
extern void* Rogue_call_ROGUEM10( int i, void* THIS, void* p0, RogueInt32 p1 )
{
  return ((ROGUEM10)(((RogueObject*)THIS)->type->methods[i]))( THIS, p0, p1 );
}

// (Function(TableEntry<<String,Value>>,TableEntry<<String,Value>>)->Logical).call(TableEntry<<String,Value>>,TableEntry<<String,Value>>)
extern RogueLogical Rogue_call_ROGUEM11( int i, void* THIS, void* p0, void* p1 )
{
  return ((ROGUEM11)(((RogueObject*)THIS)->type->methods[i]))( THIS, p0, p1 );
}


// GLOBAL PROPERTIES
RogueByte_List* RogueStringBuilder_work_bytes = 0;
RogueClassStringBuilderPool* RogueStringBuilder_pool = 0;
RogueString_List* RogueSystem_command_line_arguments = 0;
RogueString* RogueSystem_executable_filepath = 0;
RogueClassSystemEnvironment RogueSystem_environment = RogueClassSystemEnvironment();
RogueClassLogicalValue* RogueLogicalValue_true_value = 0;
RogueClassLogicalValue* RogueLogicalValue_false_value = 0;
RogueClassStringValue* RogueStringValue_empty_string = 0;

void RogueGlobal_trace( void* obj );
void RogueValueTable_trace( void* obj );
void RogueTable_String_Value__trace( void* obj );
void RogueTableEntry_String_Value__trace( void* obj );
void RogueStringBuilder_trace( void* obj );
void RogueByte_List_trace( void* obj );
void RogueStringBuilderPool_trace( void* obj );
void RogueStringBuilder_List_trace( void* obj );
void Rogue_Function____List_trace( void* obj );
void RogueException_trace( void* obj );
void RogueStackTrace_trace( void* obj );
void RogueString_List_trace( void* obj );
void RogueCharacter_List_trace( void* obj );
void RogueValueList_trace( void* obj );
void RogueValue_List_trace( void* obj );
void RogueStringConsolidationTable_trace( void* obj );
void RogueStringTable_String__trace( void* obj );
void RogueTable_String_String__trace( void* obj );
void RogueTableEntry_String_String__trace( void* obj );
void RogueConsole_trace( void* obj );
void RogueConsoleErrorPrinter_trace( void* obj );
void RoguePrimitiveWorkBuffer_trace( void* obj );
void RogueError_trace( void* obj );
void RogueFile_trace( void* obj );
void RogueLineReader_trace( void* obj );
void RogueFileWriter_trace( void* obj );
void RogueScanner_trace( void* obj );
void RogueJSONParser_trace( void* obj );
void RogueWeakReference_trace( void* obj );
void RoguePrintWriterAdapter_trace( void* obj );
void RogueStringValue_trace( void* obj );
void RogueOutOfBoundsError_trace( void* obj );
void RogueListRewriter_Character__trace( void* obj );
void RogueFunction_1184_trace( void* obj );
void RogueIOError_trace( void* obj );
void RogueFileReader_trace( void* obj );
void RogueUTF8Reader_trace( void* obj );
void RogueJSONParseError_trace( void* obj );
void RogueJSONParserBuffer_trace( void* obj );

void RogueGlobal_trace( void* obj )
{
  void* link;
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  if ((link=((RogueClassGlobal*)obj)->config)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=((RogueClassGlobal*)obj)->cache)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=((RogueClassGlobal*)obj)->console)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=((RogueClassGlobal*)obj)->global_output_buffer)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=((RogueClassGlobal*)obj)->exit_functions)) ((RogueObject*)link)->type->trace_fn( link );
}

void RogueValueTable_trace( void* obj )
{
  void* link;
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  if ((link=((RogueClassValueTable*)obj)->data)) ((RogueObject*)link)->type->trace_fn( link );
}

void RogueTable_String_Value__trace( void* obj )
{
  void* link;
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  if ((link=((RogueClassTable_String_Value_*)obj)->bins)) RogueArray_trace( link );
  if ((link=((RogueClassTable_String_Value_*)obj)->first_entry)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=((RogueClassTable_String_Value_*)obj)->last_entry)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=((RogueClassTable_String_Value_*)obj)->cur_entry)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=((RogueClassTable_String_Value_*)obj)->sort_function)) ((RogueObject*)link)->type->trace_fn( link );
}

void RogueTableEntry_String_Value__trace( void* obj )
{
  void* link;
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  if ((link=((RogueClassTableEntry_String_Value_*)obj)->key)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=((RogueClassTableEntry_String_Value_*)obj)->value)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=((RogueClassTableEntry_String_Value_*)obj)->adjacent_entry)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=((RogueClassTableEntry_String_Value_*)obj)->next_entry)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=((RogueClassTableEntry_String_Value_*)obj)->previous_entry)) ((RogueObject*)link)->type->trace_fn( link );
}

void RogueStringBuilder_trace( void* obj )
{
  void* link;
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  if ((link=((RogueStringBuilder*)obj)->utf8)) ((RogueObject*)link)->type->trace_fn( link );
}

void RogueByte_List_trace( void* obj )
{
  void* link;
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  if ((link=((RogueByte_List*)obj)->data)) RogueArray_trace( link );
}

void RogueStringBuilderPool_trace( void* obj )
{
  void* link;
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  if ((link=((RogueClassStringBuilderPool*)obj)->available)) ((RogueObject*)link)->type->trace_fn( link );
}

void RogueStringBuilder_List_trace( void* obj )
{
  void* link;
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  if ((link=((RogueStringBuilder_List*)obj)->data)) RogueArray_trace( link );
}

void Rogue_Function____List_trace( void* obj )
{
  void* link;
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  if ((link=((Rogue_Function____List*)obj)->data)) RogueArray_trace( link );
}

void RogueException_trace( void* obj )
{
  void* link;
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  if ((link=((RogueException*)obj)->message)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=((RogueException*)obj)->stack_trace)) ((RogueObject*)link)->type->trace_fn( link );
}

void RogueStackTrace_trace( void* obj )
{
  void* link;
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  if ((link=((RogueClassStackTrace*)obj)->entries)) ((RogueObject*)link)->type->trace_fn( link );
}

void RogueString_List_trace( void* obj )
{
  void* link;
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  if ((link=((RogueString_List*)obj)->data)) RogueArray_trace( link );
}

void RogueCharacter_List_trace( void* obj )
{
  void* link;
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  if ((link=((RogueCharacter_List*)obj)->data)) RogueArray_trace( link );
}

void RogueValueList_trace( void* obj )
{
  void* link;
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  if ((link=((RogueClassValueList*)obj)->data)) ((RogueObject*)link)->type->trace_fn( link );
}

void RogueValue_List_trace( void* obj )
{
  void* link;
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  if ((link=((RogueValue_List*)obj)->data)) RogueArray_trace( link );
}

void RogueStringConsolidationTable_trace( void* obj )
{
  void* link;
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  if ((link=((RogueClassStringConsolidationTable*)obj)->bins)) RogueArray_trace( link );
  if ((link=((RogueClassStringConsolidationTable*)obj)->first_entry)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=((RogueClassStringConsolidationTable*)obj)->last_entry)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=((RogueClassStringConsolidationTable*)obj)->cur_entry)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=((RogueClassStringConsolidationTable*)obj)->sort_function)) ((RogueObject*)link)->type->trace_fn( link );
}

void RogueStringTable_String__trace( void* obj )
{
  void* link;
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  if ((link=((RogueClassStringTable_String_*)obj)->bins)) RogueArray_trace( link );
  if ((link=((RogueClassStringTable_String_*)obj)->first_entry)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=((RogueClassStringTable_String_*)obj)->last_entry)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=((RogueClassStringTable_String_*)obj)->cur_entry)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=((RogueClassStringTable_String_*)obj)->sort_function)) ((RogueObject*)link)->type->trace_fn( link );
}

void RogueTable_String_String__trace( void* obj )
{
  void* link;
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  if ((link=((RogueClassTable_String_String_*)obj)->bins)) RogueArray_trace( link );
  if ((link=((RogueClassTable_String_String_*)obj)->first_entry)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=((RogueClassTable_String_String_*)obj)->last_entry)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=((RogueClassTable_String_String_*)obj)->cur_entry)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=((RogueClassTable_String_String_*)obj)->sort_function)) ((RogueObject*)link)->type->trace_fn( link );
}

void RogueTableEntry_String_String__trace( void* obj )
{
  void* link;
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  if ((link=((RogueClassTableEntry_String_String_*)obj)->key)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=((RogueClassTableEntry_String_String_*)obj)->value)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=((RogueClassTableEntry_String_String_*)obj)->adjacent_entry)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=((RogueClassTableEntry_String_String_*)obj)->next_entry)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=((RogueClassTableEntry_String_String_*)obj)->previous_entry)) ((RogueObject*)link)->type->trace_fn( link );
}

void RogueConsole_trace( void* obj )
{
  void* link;
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  if ((link=((RogueClassConsole*)obj)->error)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=((RogueClassConsole*)obj)->output_buffer)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=((RogueClassConsole*)obj)->input_buffer)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=((RogueClassConsole*)obj)->input_bytes)) ((RogueObject*)link)->type->trace_fn( link );
}

void RogueConsoleErrorPrinter_trace( void* obj )
{
  void* link;
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  if ((link=((RogueClassConsoleErrorPrinter*)obj)->output_buffer)) ((RogueObject*)link)->type->trace_fn( link );
}

void RoguePrimitiveWorkBuffer_trace( void* obj )
{
  void* link;
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  if ((link=((RogueClassPrimitiveWorkBuffer*)obj)->utf8)) ((RogueObject*)link)->type->trace_fn( link );
}

void RogueError_trace( void* obj )
{
  void* link;
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  if ((link=((RogueClassError*)obj)->message)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=((RogueClassError*)obj)->stack_trace)) ((RogueObject*)link)->type->trace_fn( link );
}

void RogueFile_trace( void* obj )
{
  void* link;
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  if ((link=((RogueClassFile*)obj)->filepath)) ((RogueObject*)link)->type->trace_fn( link );
}

void RogueLineReader_trace( void* obj )
{
  void* link;
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  if ((link=((RogueClassLineReader*)obj)->source)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=((RogueClassLineReader*)obj)->next)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=((RogueClassLineReader*)obj)->buffer)) ((RogueObject*)link)->type->trace_fn( link );
}

void RogueFileWriter_trace( void* obj )
{
  void* link;
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  if ((link=((RogueClassFileWriter*)obj)->filepath)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=((RogueClassFileWriter*)obj)->buffer)) ((RogueObject*)link)->type->trace_fn( link );
}

void RogueScanner_trace( void* obj )
{
  void* link;
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  if ((link=((RogueClassScanner*)obj)->data)) ((RogueObject*)link)->type->trace_fn( link );
}

void RogueJSONParser_trace( void* obj )
{
  void* link;
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  if ((link=((RogueClassJSONParser*)obj)->reader)) ((RogueObject*)link)->type->trace_fn( link );
}

void RogueWeakReference_trace( void* obj )
{
  void* link;
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  if ((link=((RogueWeakReference*)obj)->next_weak_reference)) ((RogueObject*)link)->type->trace_fn( link );
}

void RoguePrintWriterAdapter_trace( void* obj )
{
  void* link;
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  if ((link=((RogueClassPrintWriterAdapter*)obj)->output)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=((RogueClassPrintWriterAdapter*)obj)->buffer)) ((RogueObject*)link)->type->trace_fn( link );
}

void RogueStringValue_trace( void* obj )
{
  void* link;
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  if ((link=((RogueClassStringValue*)obj)->value)) ((RogueObject*)link)->type->trace_fn( link );
}

void RogueOutOfBoundsError_trace( void* obj )
{
  void* link;
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  if ((link=((RogueClassOutOfBoundsError*)obj)->message)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=((RogueClassOutOfBoundsError*)obj)->stack_trace)) ((RogueObject*)link)->type->trace_fn( link );
}

void RogueListRewriter_Character__trace( void* obj )
{
  void* link;
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  if ((link=((RogueClassListRewriter_Character_*)obj)->list)) ((RogueObject*)link)->type->trace_fn( link );
}

void RogueFunction_1184_trace( void* obj )
{
  void* link;
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  if ((link=((RogueClassFunction_1184*)obj)->console)) ((RogueObject*)link)->type->trace_fn( link );
}

void RogueIOError_trace( void* obj )
{
  void* link;
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  if ((link=((RogueClassIOError*)obj)->message)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=((RogueClassIOError*)obj)->stack_trace)) ((RogueObject*)link)->type->trace_fn( link );
}

void RogueFileReader_trace( void* obj )
{
  void* link;
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  if ((link=((RogueClassFileReader*)obj)->filepath)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=((RogueClassFileReader*)obj)->buffer)) ((RogueObject*)link)->type->trace_fn( link );
}

void RogueUTF8Reader_trace( void* obj )
{
  void* link;
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  if ((link=((RogueClassUTF8Reader*)obj)->byte_reader)) ((RogueObject*)link)->type->trace_fn( link );
}

void RogueJSONParseError_trace( void* obj )
{
  void* link;
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  if ((link=((RogueClassJSONParseError*)obj)->message)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=((RogueClassJSONParseError*)obj)->stack_trace)) ((RogueObject*)link)->type->trace_fn( link );
}

void RogueJSONParserBuffer_trace( void* obj )
{
  void* link;
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  if ((link=((RogueClassJSONParserBuffer*)obj)->utf8)) ((RogueObject*)link)->type->trace_fn( link );
}


const int Rogue_type_name_index_table[] =
{
  9,134,210,211,167,135,151,212,187,139,158,136,159,137,194,138,
  213,186,156,199,188,214,198,144,185,4,141,197,184,215,216,217,
  195,189,166,196,190,209,203,140,191,160,161,218,219,142,220,153,
  192,143,200,145,202,146,147,221,148,222,149,150,152,154,207,155,
  157,162,223,168,169,170,171,208,204,163,201,205,164,165,206,193,
  224,225,226,227
};
RogueInitFn Rogue_init_object_fn_table[] =
{
  0,
  (RogueInitFn) RogueGlobal__init_object,
  0,
  0,
  (RogueInitFn) RogueValueTable__init_object,
  (RogueInitFn) RogueValue__init_object,
  (RogueInitFn) RogueTable_String_Value___init_object,
  0,
  0,
  0,
  (RogueInitFn) RogueTableEntry_String_Value___init_object,
  0,
  (RogueInitFn) Rogue_Function_TableEntry_String_Value__TableEntry_String_Value__RETURNSLogical___init_object,
  (RogueInitFn) RogueStringBuilder__init_object,
  (RogueInitFn) RogueByte_List__init_object,
  (RogueInitFn) RogueGenericList__init_object,
  0,
  0,
  (RogueInitFn) RogueStringBuilderPool__init_object,
  (RogueInitFn) RogueStringBuilder_List__init_object,
  0,
  0,
  (RogueInitFn) Rogue_Function____List__init_object,
  (RogueInitFn) Rogue_Function_____init_object,
  0,
  (RogueInitFn) RogueException__init_object,
  (RogueInitFn) RogueStackTrace__init_object,
  (RogueInitFn) RogueString_List__init_object,
  0,
  0,
  0,
  0,
  (RogueInitFn) RogueCharacter_List__init_object,
  0,
  (RogueInitFn) RogueValueList__init_object,
  (RogueInitFn) RogueValue_List__init_object,
  0,
  (RogueInitFn) RogueStringConsolidationTable__init_object,
  (RogueInitFn) RogueStringTable_String___init_object,
  (RogueInitFn) RogueTable_String_String___init_object,
  0,
  (RogueInitFn) RogueTableEntry_String_String___init_object,
  (RogueInitFn) Rogue_Function_TableEntry_String_String__TableEntry_String_String__RETURNSLogical___init_object,
  0,
  0,
  (RogueInitFn) RogueConsole__init_object,
  0,
  (RogueInitFn) RogueConsoleErrorPrinter__init_object,
  (RogueInitFn) RoguePrimitiveWorkBuffer__init_object,
  (RogueInitFn) RogueMath__init_object,
  (RogueInitFn) RogueFunction_221__init_object,
  (RogueInitFn) RogueSystem__init_object,
  (RogueInitFn) RogueError__init_object,
  (RogueInitFn) RogueFile__init_object,
  (RogueInitFn) RogueLineReader__init_object,
  0,
  (RogueInitFn) RogueFileWriter__init_object,
  0,
  (RogueInitFn) RogueScanner__init_object,
  (RogueInitFn) RogueJSONParser__init_object,
  (RogueInitFn) RogueJSON__init_object,
  (RogueInitFn) Rogue_Function_String_RETURNSString___init_object,
  (RogueInitFn) RogueFunction_283__init_object,
  (RogueInitFn) RogueRuntime__init_object,
  (RogueInitFn) RogueWeakReference__init_object,
  (RogueInitFn) RoguePrintWriterAdapter__init_object,
  0,
  (RogueInitFn) RogueLogicalValue__init_object,
  (RogueInitFn) RogueReal64Value__init_object,
  (RogueInitFn) RogueNullValue__init_object,
  (RogueInitFn) RogueStringValue__init_object,
  (RogueInitFn) RogueUndefinedValue__init_object,
  (RogueInitFn) RogueOutOfBoundsError__init_object,
  (RogueInitFn) RogueListRewriter_Character___init_object,
  (RogueInitFn) RogueFunction_1184__init_object,
  (RogueInitFn) RogueIOError__init_object,
  (RogueInitFn) RogueFileReader__init_object,
  (RogueInitFn) RogueUTF8Reader__init_object,
  (RogueInitFn) RogueJSONParseError__init_object,
  (RogueInitFn) RogueJSONParserBuffer__init_object,
  0,
  0,
  0,
  0
};

RogueInitFn Rogue_init_fn_table[] =
{
  0,
  (RogueInitFn) RogueGlobal__init,
  0,
  0,
  (RogueInitFn) RogueValueTable__init,
  (RogueInitFn) RogueObject__init,
  (RogueInitFn) RogueTable_String_Value___init,
  0,
  0,
  0,
  (RogueInitFn) RogueObject__init,
  0,
  (RogueInitFn) RogueObject__init,
  (RogueInitFn) RogueStringBuilder__init,
  (RogueInitFn) RogueByte_List__init,
  (RogueInitFn) RogueObject__init,
  0,
  0,
  (RogueInitFn) RogueObject__init,
  (RogueInitFn) RogueStringBuilder_List__init,
  0,
  0,
  (RogueInitFn) Rogue_Function____List__init,
  (RogueInitFn) RogueObject__init,
  0,
  (RogueInitFn) RogueException__init,
  (RogueInitFn) RogueObject__init,
  (RogueInitFn) RogueString_List__init,
  0,
  0,
  0,
  0,
  (RogueInitFn) RogueCharacter_List__init,
  0,
  (RogueInitFn) RogueValueList__init,
  (RogueInitFn) RogueValue_List__init,
  0,
  (RogueInitFn) RogueTable_String_String___init,
  (RogueInitFn) RogueTable_String_String___init,
  (RogueInitFn) RogueTable_String_String___init,
  0,
  (RogueInitFn) RogueObject__init,
  (RogueInitFn) RogueObject__init,
  0,
  0,
  (RogueInitFn) RogueConsole__init,
  0,
  (RogueInitFn) RogueObject__init,
  (RogueInitFn) RogueStringBuilder__init,
  (RogueInitFn) RogueObject__init,
  (RogueInitFn) RogueObject__init,
  (RogueInitFn) RogueObject__init,
  (RogueInitFn) RogueException__init,
  (RogueInitFn) RogueObject__init,
  (RogueInitFn) RogueObject__init,
  0,
  (RogueInitFn) RogueObject__init,
  0,
  (RogueInitFn) RogueObject__init,
  (RogueInitFn) RogueObject__init,
  (RogueInitFn) RogueObject__init,
  (RogueInitFn) RogueObject__init,
  (RogueInitFn) RogueObject__init,
  (RogueInitFn) RogueObject__init,
  (RogueInitFn) RogueObject__init,
  (RogueInitFn) RogueObject__init,
  0,
  (RogueInitFn) RogueObject__init,
  (RogueInitFn) RogueObject__init,
  (RogueInitFn) RogueObject__init,
  (RogueInitFn) RogueObject__init,
  (RogueInitFn) RogueObject__init,
  (RogueInitFn) RogueException__init,
  (RogueInitFn) RogueObject__init,
  (RogueInitFn) RogueObject__init,
  (RogueInitFn) RogueException__init,
  (RogueInitFn) RogueObject__init,
  (RogueInitFn) RogueObject__init,
  (RogueInitFn) RogueException__init,
  (RogueInitFn) RogueStringBuilder__init,
  0,
  0,
  0,
  0
};

RogueCleanUpFn Rogue_on_cleanup_fn_table[] =
{
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  (RogueCleanUpFn) RogueFileWriter__on_cleanup,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  (RogueCleanUpFn) RogueWeakReference__on_cleanup,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  (RogueCleanUpFn) RogueFileReader__on_cleanup,
  0,
  0,
  0,
  0,
  0,
  0,
  0
};

RogueToStringFn Rogue_to_string_fn_table[] =
{
  0,
  (RogueToStringFn) RogueObject__to_String,
  0,
  0,
  (RogueToStringFn) RogueValueTable__to_String,
  (RogueToStringFn) RogueObject__to_String,
  (RogueToStringFn) RogueTable_String_Value___to_String,
  0,
  0,
  0,
  (RogueToStringFn) RogueTableEntry_String_Value___to_String,
  0,
  (RogueToStringFn) RogueObject__to_String,
  (RogueToStringFn) RogueStringBuilder__to_String,
  (RogueToStringFn) RogueByte_List__to_String,
  (RogueToStringFn) RogueObject__to_String,
  0,
  0,
  (RogueToStringFn) RogueObject__to_String,
  (RogueToStringFn) RogueStringBuilder_List__to_String,
  0,
  0,
  (RogueToStringFn) Rogue_Function____List__to_String,
  (RogueToStringFn) RogueObject__to_String,
  0,
  (RogueToStringFn) RogueException__to_String,
  (RogueToStringFn) RogueStackTrace__to_String,
  (RogueToStringFn) RogueString_List__to_String,
  0,
  0,
  0,
  0,
  (RogueToStringFn) RogueCharacter_List__to_String,
  0,
  (RogueToStringFn) RogueValueList__to_String,
  (RogueToStringFn) RogueValue_List__to_String,
  0,
  (RogueToStringFn) RogueTable_String_String___to_String,
  (RogueToStringFn) RogueTable_String_String___to_String,
  (RogueToStringFn) RogueTable_String_String___to_String,
  0,
  (RogueToStringFn) RogueTableEntry_String_String___to_String,
  (RogueToStringFn) RogueObject__to_String,
  0,
  0,
  (RogueToStringFn) RogueObject__to_String,
  0,
  (RogueToStringFn) RogueObject__to_String,
  (RogueToStringFn) RogueStringBuilder__to_String,
  (RogueToStringFn) RogueObject__to_String,
  (RogueToStringFn) RogueObject__to_String,
  (RogueToStringFn) RogueObject__to_String,
  (RogueToStringFn) RogueException__to_String,
  (RogueToStringFn) RogueFile__to_String,
  (RogueToStringFn) RogueObject__to_String,
  0,
  (RogueToStringFn) RogueObject__to_String,
  0,
  (RogueToStringFn) RogueObject__to_String,
  (RogueToStringFn) RogueObject__to_String,
  (RogueToStringFn) RogueObject__to_String,
  (RogueToStringFn) RogueObject__to_String,
  (RogueToStringFn) RogueObject__to_String,
  (RogueToStringFn) RogueObject__to_String,
  (RogueToStringFn) RogueObject__to_String,
  (RogueToStringFn) RogueObject__to_String,
  0,
  (RogueToStringFn) RogueLogicalValue__to_String,
  (RogueToStringFn) RogueReal64Value__to_String,
  (RogueToStringFn) RogueNullValue__to_String,
  (RogueToStringFn) RogueStringValue__to_String,
  (RogueToStringFn) RogueUndefinedValue__to_String,
  (RogueToStringFn) RogueException__to_String,
  (RogueToStringFn) RogueObject__to_String,
  (RogueToStringFn) RogueObject__to_String,
  (RogueToStringFn) RogueException__to_String,
  (RogueToStringFn) RogueObject__to_String,
  (RogueToStringFn) RogueObject__to_String,
  (RogueToStringFn) RogueException__to_String,
  (RogueToStringFn) RogueStringBuilder__to_String,
  0,
  0,
  0,
  0
};

RogueTraceFn Rogue_trace_fn_table[] =
{
  RogueObject_trace,
  RogueGlobal_trace,
  0,
  0,
  RogueValueTable_trace,
  RogueObject_trace,
  RogueTable_String_Value__trace,
  0,
  RogueArray_trace,
  RogueObject_trace,
  RogueTableEntry_String_Value__trace,
  RogueObject_trace,
  RogueObject_trace,
  RogueStringBuilder_trace,
  RogueByte_List_trace,
  RogueObject_trace,
  0,
  RogueObject_trace,
  RogueStringBuilderPool_trace,
  RogueStringBuilder_List_trace,
  RogueArray_trace,
  0,
  Rogue_Function____List_trace,
  RogueObject_trace,
  RogueObject_trace,
  RogueException_trace,
  RogueStackTrace_trace,
  RogueString_List_trace,
  RogueObject_trace,
  0,
  0,
  0,
  RogueCharacter_List_trace,
  RogueObject_trace,
  RogueValueList_trace,
  RogueValue_List_trace,
  RogueObject_trace,
  RogueStringConsolidationTable_trace,
  RogueStringTable_String__trace,
  RogueTable_String_String__trace,
  RogueArray_trace,
  RogueTableEntry_String_String__trace,
  RogueObject_trace,
  0,
  0,
  RogueConsole_trace,
  0,
  RogueConsoleErrorPrinter_trace,
  RoguePrimitiveWorkBuffer_trace,
  RogueObject_trace,
  RogueObject_trace,
  RogueObject_trace,
  RogueError_trace,
  RogueFile_trace,
  RogueLineReader_trace,
  0,
  RogueFileWriter_trace,
  0,
  RogueScanner_trace,
  RogueJSONParser_trace,
  RogueObject_trace,
  RogueObject_trace,
  RogueObject_trace,
  RogueObject_trace,
  RogueWeakReference_trace,
  RoguePrintWriterAdapter_trace,
  0,
  RogueObject_trace,
  RogueObject_trace,
  RogueObject_trace,
  RogueStringValue_trace,
  RogueObject_trace,
  RogueOutOfBoundsError_trace,
  RogueListRewriter_Character__trace,
  RogueFunction_1184_trace,
  RogueIOError_trace,
  RogueFileReader_trace,
  RogueUTF8Reader_trace,
  RogueJSONParseError_trace,
  RogueJSONParserBuffer_trace,
  0,
  0,
  0,
  0
};

void Rogue_trace()
{
  void* link;
  int i;

  // Trace GLOBAL PROPERTIES
  if ((link=RogueStringBuilder_work_bytes)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=RogueStringBuilder_pool)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=RogueSystem_command_line_arguments)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=RogueSystem_executable_filepath)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=RogueLogicalValue_true_value)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=RogueLogicalValue_false_value)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=RogueStringValue_empty_string)) ((RogueObject*)link)->type->trace_fn( link );

  // Trace Class objects and singletons
  for (i=Rogue_type_count; --i>=0; )
  {
    RogueType* type = &Rogue_types[i];
    {
      auto singleton = ROGUE_GET_SINGLETON(type);
      if (singleton) type->trace_fn( singleton );
    }
  }
}

const void* Rogue_dynamic_method_table[] =
{
  (void*) (ROGUEM0) RogueObject__init_object, // Object
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueObject__type_name,
  (void*) (ROGUEM1) RogueGlobal__init_object, // Global
  (void*) (ROGUEM1) RogueGlobal__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueGlobal__type_name,
  0, // PrintWriter<<global_output_buffer>>.close() // PrintWriter<<global_output_buffer>>
  0, // PrintWriter.close() // PrintWriter
  (void*) (ROGUEM1) RogueValueTable__init_object, // ValueTable
  (void*) (ROGUEM1) RogueValueTable__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Value.operator==(Object)
  (void*) (ROGUEM1) RogueValueTable__to_String,
  0, // Value.to_Value()
  0, // Value.to_ValueList()
  0, // ValueTable.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueValueTable__type_name,
  0, // Value.add(Value)
  0, // Value.add(Value[])
  0, // Value.add_all(Value)
  0, // ValueTable.clear()
  0, // ValueTable.cloned()
  0, // ValueTable.apply((Function(Value)->Value))
  (void*) (ROGUEM2) RogueValueTable__contains__String,
  (void*) (ROGUEM2) RogueValueTable__contains__Value,
  0, // Value.contains((Function(Value)->Logical))
  (void*) (ROGUEM3) RogueValueTable__count,
  0, // ValueTable.count((Function(Value)->Logical))
  0, // Value.compressed()
  0, // Value.decompressed()
  0, // ValueTable.decode_indexed(ValueIDLookupTable)
  0, // ValueTable.encode_indexed(ValueIDTableBuilder)
  (void*) (ROGUEM4) RogueValueTable__ensure_list__String,
  (void*) (ROGUEM4) RogueValueTable__ensure_table__String,
  0, // Value.first()
  0, // ValueTable.first((Function(Value)->Logical))
  (void*) (ROGUEM5) RogueValueTable__get__Int32,
  (void*) (ROGUEM4) RogueValueTable__get__String,
  0, // ValueTable.get((Function(Value)->Logical))
  0, // Value.insert(Value,Int32)
  0, // Value.insert(Value[],Int32)
  0, // Value.insert_all(Value,Int32)
  (void*) (ROGUEM6) RogueValueTable__is_collection,
  0, // Value.is_complex()
  0, // Value.is_int32()
  0, // Value.is_int64()
  0, // Value.is_list()
  (void*) (ROGUEM6) RogueValue__is_logical,
  (void*) (ROGUEM6) RogueValue__is_null,
  (void*) (ROGUEM6) RogueValue__is_non_null,
  (void*) (ROGUEM6) RogueValue__is_number,
  0, // Value.is_object()
  0, // Value.is_real64()
  (void*) (ROGUEM6) RogueValue__is_string,
  0, // ValueTable.is_table()
  0, // Value.is_undefined()
  (void*) (ROGUEM1) RogueValueTable__keys,
  0, // Value.last()
  0, // ValueTable.last((Function(Value)->Logical))
  0, // ValueTable.locate(Value)
  0, // ValueTable.locate((Function(Value)->Logical))
  0, // ValueTable.locate_last(Value)
  0, // ValueTable.locate_last((Function(Value)->Logical))
  (void*) (ROGUEM2) RogueValue__operatorEQUALSEQUALS__Value,
  0, // Value.operator==(Byte)
  0, // Value.operator==(Character)
  0, // Value.operator==(Int32)
  0, // Value.operator==(Int64)
  0, // Value.operator==(Logical)
  0, // Value.operator==(Real64)
  0, // Value.operator==(Real32)
  0, // Value.operator==(String)
  0, // Value.operator<(Value)
  0, // Value.operator<(Byte)
  0, // Value.operator<(Character)
  0, // Value.operator<(Int32)
  0, // Value.operator<(Int64)
  0, // Value.operator<(Logical)
  0, // Value.operator<(Real64)
  0, // Value.operator<(Real32)
  0, // Value.operator<(Object)
  0, // Value.operator<(String)
  0, // Value.operator-()
  0, // Value.operator+(Value)
  0, // Value.operator-(Value)
  0, // Value.operator*(Value)
  0, // Value.operator/(Value)
  0, // Value.operator%(Value)
  0, // Value.operator+(Real64)
  0, // Value.operator-(Real64)
  0, // Value.operator*(Real64)
  0, // Value.operator/(Real64)
  0, // Value.operator%(Real64)
  0, // Value.operator+(String)
  0, // Value.operator*(String)
  (void*) (ROGUEM4) RogueValue__remove__Value,
  0, // ValueTable.remove(String)
  0, // ValueTable.remove((Function(Value)->Logical))
  0, // ValueTable.remove_at(Int32)
  0, // Value.remove_first()
  0, // Value.remove_last()
  0, // Value.reserve(Int32)
  0, // Value.rest(Value)
  0, // Value.save(File,Logical,Logical)
  0, // ValueTable.set(Int32,Value)
  (void*) (ROGUEM7) RogueValueTable__set__String_Value,
  0, // ValueTable.sort((Function(Value,Value)->Logical))
  0, // Value.sorted((Function(Value,Value)->Logical))
  0, // Value.to_Byte()
  0, // Value.to_Character()
  (void*) (ROGUEM8) RogueValue__to_Int64,
  (void*) (ROGUEM3) RogueValue__to_Int32,
  (void*) (ROGUEM6) RogueValueTable__to_Logical,
  (void*) (ROGUEM9) RogueValue__to_Real64,
  0, // Value.to_Real32()
  0, // Value.to_Object()
  0, // Value.to_json(Int32)
  0, // Value.to_json(Logical,Logical)
  0, // Value.to_json(StringBuilder,Logical,Logical)
  (void*) (ROGUEM10) RogueValueTable__to_json__StringBuilder_Int32,
  (void*) (ROGUEM1) RogueValue__init_object, // Value
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Value.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Value.to_Value()
  0, // Value.to_ValueList()
  0, // Value.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueValue__type_name,
  0, // Value.add(Value)
  0, // Value.add(Value[])
  0, // Value.add_all(Value)
  0, // Value.clear()
  0, // Value.cloned()
  0, // Value.apply((Function(Value)->Value))
  (void*) (ROGUEM2) RogueValue__contains__String,
  (void*) (ROGUEM2) RogueValue__contains__Value,
  0, // Value.contains((Function(Value)->Logical))
  (void*) (ROGUEM3) RogueValue__count,
  0, // Value.count((Function(Value)->Logical))
  0, // Value.compressed()
  0, // Value.decompressed()
  0, // Value.decode_indexed(ValueIDLookupTable)
  0, // Value.encode_indexed(ValueIDTableBuilder)
  0, // Value.ensure_list(String)
  (void*) (ROGUEM4) RogueValue__ensure_table__String,
  0, // Value.first()
  0, // Value.first((Function(Value)->Logical))
  (void*) (ROGUEM5) RogueValue__get__Int32,
  (void*) (ROGUEM4) RogueValue__get__String,
  0, // Value.get((Function(Value)->Logical))
  0, // Value.insert(Value,Int32)
  0, // Value.insert(Value[],Int32)
  0, // Value.insert_all(Value,Int32)
  (void*) (ROGUEM6) RogueValue__is_collection,
  0, // Value.is_complex()
  0, // Value.is_int32()
  0, // Value.is_int64()
  0, // Value.is_list()
  (void*) (ROGUEM6) RogueValue__is_logical,
  (void*) (ROGUEM6) RogueValue__is_null,
  (void*) (ROGUEM6) RogueValue__is_non_null,
  (void*) (ROGUEM6) RogueValue__is_number,
  0, // Value.is_object()
  0, // Value.is_real64()
  (void*) (ROGUEM6) RogueValue__is_string,
  0, // Value.is_table()
  0, // Value.is_undefined()
  0, // Value.keys()
  0, // Value.last()
  0, // Value.last((Function(Value)->Logical))
  0, // Value.locate(Value)
  0, // Value.locate((Function(Value)->Logical))
  0, // Value.locate_last(Value)
  0, // Value.locate_last((Function(Value)->Logical))
  (void*) (ROGUEM2) RogueValue__operatorEQUALSEQUALS__Value,
  0, // Value.operator==(Byte)
  0, // Value.operator==(Character)
  0, // Value.operator==(Int32)
  0, // Value.operator==(Int64)
  0, // Value.operator==(Logical)
  0, // Value.operator==(Real64)
  0, // Value.operator==(Real32)
  0, // Value.operator==(String)
  0, // Value.operator<(Value)
  0, // Value.operator<(Byte)
  0, // Value.operator<(Character)
  0, // Value.operator<(Int32)
  0, // Value.operator<(Int64)
  0, // Value.operator<(Logical)
  0, // Value.operator<(Real64)
  0, // Value.operator<(Real32)
  0, // Value.operator<(Object)
  0, // Value.operator<(String)
  0, // Value.operator-()
  0, // Value.operator+(Value)
  0, // Value.operator-(Value)
  0, // Value.operator*(Value)
  0, // Value.operator/(Value)
  0, // Value.operator%(Value)
  0, // Value.operator+(Real64)
  0, // Value.operator-(Real64)
  0, // Value.operator*(Real64)
  0, // Value.operator/(Real64)
  0, // Value.operator%(Real64)
  0, // Value.operator+(String)
  0, // Value.operator*(String)
  (void*) (ROGUEM4) RogueValue__remove__Value,
  0, // Value.remove(String)
  0, // Value.remove((Function(Value)->Logical))
  0, // Value.remove_at(Int32)
  0, // Value.remove_first()
  0, // Value.remove_last()
  0, // Value.reserve(Int32)
  0, // Value.rest(Value)
  0, // Value.save(File,Logical,Logical)
  0, // Value.set(Int32,Value)
  (void*) (ROGUEM7) RogueValue__set__String_Value,
  0, // Value.sort((Function(Value,Value)->Logical))
  0, // Value.sorted((Function(Value,Value)->Logical))
  0, // Value.to_Byte()
  0, // Value.to_Character()
  (void*) (ROGUEM8) RogueValue__to_Int64,
  (void*) (ROGUEM3) RogueValue__to_Int32,
  (void*) (ROGUEM6) RogueValue__to_Logical,
  (void*) (ROGUEM9) RogueValue__to_Real64,
  0, // Value.to_Real32()
  0, // Value.to_Object()
  0, // Value.to_json(Int32)
  0, // Value.to_json(Logical,Logical)
  0, // Value.to_json(StringBuilder,Logical,Logical)
  (void*) (ROGUEM10) RogueValue__to_json__StringBuilder_Int32,
  (void*) (ROGUEM1) RogueTable_String_Value___init_object, // Table<<String,Value>>
  (void*) (ROGUEM1) RogueTable_String_Value___init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueTable_String_Value___to_String,
  0, // Table<<String,Value>>.to_Value()
  0, // Object.to_ValueList()
  0, // Table<<String,Value>>.to_ValueTable()
  0, // Object.type_info()
  0, // Table<<String,Value>>.unpack(Value)
  (void*) (ROGUEM1) RogueTable_String_Value___type_name,
  0, // Int32.or_smaller(Int32) // Int32
  (void*) (ROGUEM0) RogueObject__init_object, // Array<<TableEntry<<String,Value>>>>
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueArray_TableEntry_String_Value____type_name,
  (void*) (ROGUEM0) RogueObject__init_object, // Array
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueArray__type_name,
  (void*) (ROGUEM1) RogueTableEntry_String_Value___init_object, // TableEntry<<String,Value>>
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueTableEntry_String_Value___to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueTableEntry_String_Value___type_name,
  (void*) (ROGUEM0) RogueObject__init_object, // String
  (void*) (ROGUEM1) RogueObject__init,
  (void*) (ROGUEM3) RogueString__hash_code,
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueString__to_String,
  0, // String.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueString__type_name,
  (void*) (ROGUEM1) Rogue_Function_TableEntry_String_Value__TableEntry_String_Value__RETURNSLogical___init_object, // (Function(TableEntry<<String,Value>>,TableEntry<<String,Value>>)->Logical)
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) Rogue_Function_TableEntry_String_Value__TableEntry_String_Value__RETURNSLogical___type_name,
  (void*) (ROGUEM11) Rogue_Function_TableEntry_String_Value__TableEntry_String_Value__RETURNSLogical___call__TableEntry_String_Value__TableEntry_String_Value_,
  (void*) (ROGUEM1) RogueStringBuilder__init_object, // StringBuilder
  (void*) (ROGUEM1) RogueStringBuilder__init,
  0, // StringBuilder.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueStringBuilder__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueStringBuilder__type_name,
  (void*) (ROGUEM1) RogueByte_List__init_object, // Byte[]
  (void*) (ROGUEM1) RogueByte_List__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueByte_List__to_String,
  0, // Byte[].to_Value()
  0, // Byte[].to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Byte[].unpack(Value)
  (void*) (ROGUEM1) RogueByte_List__type_name,
  (void*) (ROGUEM1) RogueGenericList__init_object, // GenericList
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueGenericList__type_name,
  0, // Byte.to_String() // Byte
  (void*) (ROGUEM0) RogueObject__init_object, // Array<<Byte>>
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueArray_Byte___type_name,
  (void*) (ROGUEM1) RogueStringBuilderPool__init_object, // StringBuilderPool
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueStringBuilderPool__type_name,
  (void*) (ROGUEM1) RogueStringBuilder_List__init_object, // StringBuilder[]
  (void*) (ROGUEM1) RogueStringBuilder_List__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueStringBuilder_List__to_String,
  0, // StringBuilder[].to_Value()
  0, // StringBuilder[].to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // StringBuilder[].unpack(Value)
  (void*) (ROGUEM1) RogueStringBuilder_List__type_name,
  (void*) (ROGUEM0) RogueObject__init_object, // Array<<StringBuilder>>
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueArray_StringBuilder___type_name,
  0, // Logical.to_String() // Logical
  (void*) (ROGUEM1) Rogue_Function____List__init_object, // (Function())[]
  (void*) (ROGUEM1) Rogue_Function____List__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) Rogue_Function____List__to_String,
  0, // (Function())[].to_Value()
  0, // (Function())[].to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // (Function())[].unpack(Value)
  (void*) (ROGUEM1) Rogue_Function____List__type_name,
  (void*) (ROGUEM1) Rogue_Function_____init_object, // (Function())
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) Rogue_Function_____type_name,
  (void*) (ROGUEM0) Rogue_Function_____call,
  (void*) (ROGUEM0) RogueObject__init_object, // Array<<(Function())>>
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueArray__Function______type_name,
  (void*) (ROGUEM1) RogueException__init_object, // Exception
  (void*) (ROGUEM1) RogueException__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueException__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueException__type_name,
  (void*) (ROGUEM4) RogueException__init__String,
  (void*) (ROGUEM1) RogueStackTrace__init_object, // StackTrace
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueStackTrace__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueStackTrace__type_name,
  (void*) (ROGUEM1) RogueString_List__init_object, // String[]
  (void*) (ROGUEM1) RogueString_List__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueString_List__to_String,
  0, // String[].to_Value()
  0, // String[].to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // String[].unpack(Value)
  (void*) (ROGUEM1) RogueString_List__type_name,
  (void*) (ROGUEM0) RogueObject__init_object, // Array<<String>>
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueArray_String___type_name,
  0, // Real64.decimal_digit_count() // Real64
  0, // Int64.print_in_power2_base(Int32,Int32,StringBuilder) // Int64
  0, // Character.is_identifier(Logical,Logical) // Character
  (void*) (ROGUEM1) RogueCharacter_List__init_object, // Character[]
  (void*) (ROGUEM1) RogueCharacter_List__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueCharacter_List__to_String,
  0, // Character[].to_Value()
  0, // Character[].to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Character[].unpack(Value)
  (void*) (ROGUEM1) RogueCharacter_List__type_name,
  (void*) (ROGUEM0) RogueObject__init_object, // Array<<Character>>
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueArray_Character___type_name,
  (void*) (ROGUEM1) RogueValueList__init_object, // ValueList
  (void*) (ROGUEM1) RogueValueList__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Value.operator==(Object)
  (void*) (ROGUEM1) RogueValueList__to_String,
  0, // Value.to_Value()
  0, // ValueList.to_ValueList()
  0, // Value.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueValueList__type_name,
  (void*) (ROGUEM4) RogueValueList__add__Value,
  0, // ValueList.add(Value[])
  0, // Value.add_all(Value)
  0, // ValueList.clear()
  0, // ValueList.cloned()
  0, // ValueList.apply((Function(Value)->Value))
  (void*) (ROGUEM2) RogueValueList__contains__String,
  (void*) (ROGUEM2) RogueValueList__contains__Value,
  0, // Value.contains((Function(Value)->Logical))
  (void*) (ROGUEM3) RogueValueList__count,
  0, // ValueList.count((Function(Value)->Logical))
  0, // Value.compressed()
  0, // Value.decompressed()
  0, // ValueList.decode_indexed(ValueIDLookupTable)
  0, // ValueList.encode_indexed(ValueIDTableBuilder)
  0, // Value.ensure_list(String)
  (void*) (ROGUEM4) RogueValue__ensure_table__String,
  0, // ValueList.first()
  0, // ValueList.first((Function(Value)->Logical))
  (void*) (ROGUEM5) RogueValueList__get__Int32,
  (void*) (ROGUEM4) RogueValue__get__String,
  0, // ValueList.get((Function(Value)->Logical))
  0, // ValueList.insert(Value,Int32)
  0, // ValueList.insert(Value[],Int32)
  0, // Value.insert_all(Value,Int32)
  (void*) (ROGUEM6) RogueValueList__is_collection,
  0, // Value.is_complex()
  0, // Value.is_int32()
  0, // Value.is_int64()
  0, // ValueList.is_list()
  (void*) (ROGUEM6) RogueValue__is_logical,
  (void*) (ROGUEM6) RogueValue__is_null,
  (void*) (ROGUEM6) RogueValue__is_non_null,
  (void*) (ROGUEM6) RogueValue__is_number,
  0, // Value.is_object()
  0, // Value.is_real64()
  (void*) (ROGUEM6) RogueValue__is_string,
  0, // Value.is_table()
  0, // Value.is_undefined()
  0, // Value.keys()
  0, // ValueList.last()
  0, // ValueList.last((Function(Value)->Logical))
  0, // ValueList.locate(Value)
  0, // ValueList.locate((Function(Value)->Logical))
  0, // ValueList.locate_last(Value)
  0, // ValueList.locate_last((Function(Value)->Logical))
  (void*) (ROGUEM2) RogueValue__operatorEQUALSEQUALS__Value,
  0, // Value.operator==(Byte)
  0, // Value.operator==(Character)
  0, // Value.operator==(Int32)
  0, // Value.operator==(Int64)
  0, // Value.operator==(Logical)
  0, // Value.operator==(Real64)
  0, // Value.operator==(Real32)
  0, // Value.operator==(String)
  0, // Value.operator<(Value)
  0, // Value.operator<(Byte)
  0, // Value.operator<(Character)
  0, // Value.operator<(Int32)
  0, // Value.operator<(Int64)
  0, // Value.operator<(Logical)
  0, // Value.operator<(Real64)
  0, // Value.operator<(Real32)
  0, // Value.operator<(Object)
  0, // Value.operator<(String)
  0, // Value.operator-()
  0, // Value.operator+(Value)
  0, // Value.operator-(Value)
  0, // Value.operator*(Value)
  0, // Value.operator/(Value)
  0, // Value.operator%(Value)
  0, // Value.operator+(Real64)
  0, // Value.operator-(Real64)
  0, // Value.operator*(Real64)
  0, // Value.operator/(Real64)
  0, // Value.operator%(Real64)
  0, // Value.operator+(String)
  0, // Value.operator*(String)
  (void*) (ROGUEM4) RogueValueList__remove__Value,
  0, // Value.remove(String)
  0, // ValueList.remove((Function(Value)->Logical))
  0, // ValueList.remove_at(Int32)
  0, // ValueList.remove_first()
  0, // ValueList.remove_last()
  0, // ValueList.reserve(Int32)
  0, // Value.rest(Value)
  0, // Value.save(File,Logical,Logical)
  0, // ValueList.set(Int32,Value)
  (void*) (ROGUEM7) RogueValue__set__String_Value,
  0, // ValueList.sort((Function(Value,Value)->Logical))
  0, // ValueList.sorted((Function(Value,Value)->Logical))
  0, // Value.to_Byte()
  0, // Value.to_Character()
  (void*) (ROGUEM8) RogueValue__to_Int64,
  (void*) (ROGUEM3) RogueValue__to_Int32,
  (void*) (ROGUEM6) RogueValueList__to_Logical,
  (void*) (ROGUEM9) RogueValue__to_Real64,
  0, // Value.to_Real32()
  0, // Value.to_Object()
  0, // Value.to_json(Int32)
  0, // Value.to_json(Logical,Logical)
  0, // Value.to_json(StringBuilder,Logical,Logical)
  (void*) (ROGUEM10) RogueValueList__to_json__StringBuilder_Int32,
  (void*) (ROGUEM1) RogueValue_List__init_object, // Value[]
  (void*) (ROGUEM1) RogueValue_List__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueValue_List__to_String,
  0, // Value[].to_Value()
  0, // Value[].to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Value[].unpack(Value)
  (void*) (ROGUEM1) RogueValue_List__type_name,
  (void*) (ROGUEM0) RogueObject__init_object, // Array<<Value>>
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueArray_Value___type_name,
  (void*) (ROGUEM1) RogueStringConsolidationTable__init_object, // StringConsolidationTable
  (void*) (ROGUEM1) RogueTable_String_String___init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueTable_String_String___to_String,
  0, // Table<<String,String>>.to_Value()
  0, // Object.to_ValueList()
  0, // Table<<String,String>>.to_ValueTable()
  0, // Object.type_info()
  0, // Table<<String,String>>.unpack(Value)
  (void*) (ROGUEM1) RogueStringConsolidationTable__type_name,
  0, // Table<<String,String>>.init(Int32)
  0, // Table<<String,String>>.init(Table<<String,String>>)
  0, // Table<<String,String>>.add(Table<<String,String>>)
  0, // Table<<String,String>>.at(Int32)
  0, // Table<<String,String>>.clear()
  0, // Table<<String,String>>.cloned()
  0, // Table<<String,String>>.contains(String)
  0, // Table<<String,String>>.contains((Function(String)->Logical))
  0, // Table<<String,String>>.count((Function(Value)->Logical))
  0, // Table<<String,String>>.discard((Function(TableEntry<<String,String>>)->Logical))
  0, // Table<<String,String>>.entries(TableEntry<<String,String>>[])
  0, // Table<<String,String>>.is_empty()
  0, // Table<<String,String>>.find(String)
  0, // Table<<String,String>>.first()
  0, // Table<<String,String>>.first((Function(String)->Logical))
  (void*) (ROGUEM4) RogueStringConsolidationTable__get__String,
  (void*) (ROGUEM1) RogueStringTable_String___init_object, // StringTable<<String>>
  (void*) (ROGUEM1) RogueTable_String_String___init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueTable_String_String___to_String,
  0, // Table<<String,String>>.to_Value()
  0, // Object.to_ValueList()
  0, // Table<<String,String>>.to_ValueTable()
  0, // Object.type_info()
  0, // Table<<String,String>>.unpack(Value)
  (void*) (ROGUEM1) RogueStringTable_String___type_name,
  0, // Table<<String,String>>.init(Int32)
  0, // Table<<String,String>>.init(Table<<String,String>>)
  0, // Table<<String,String>>.add(Table<<String,String>>)
  0, // Table<<String,String>>.at(Int32)
  0, // Table<<String,String>>.clear()
  0, // Table<<String,String>>.cloned()
  0, // Table<<String,String>>.contains(String)
  0, // Table<<String,String>>.contains((Function(String)->Logical))
  0, // Table<<String,String>>.count((Function(Value)->Logical))
  0, // Table<<String,String>>.discard((Function(TableEntry<<String,String>>)->Logical))
  0, // Table<<String,String>>.entries(TableEntry<<String,String>>[])
  0, // Table<<String,String>>.is_empty()
  0, // Table<<String,String>>.find(String)
  0, // Table<<String,String>>.first()
  0, // Table<<String,String>>.first((Function(String)->Logical))
  (void*) (ROGUEM4) RogueTable_String_String___get__String,
  (void*) (ROGUEM1) RogueTable_String_String___init_object, // Table<<String,String>>
  (void*) (ROGUEM1) RogueTable_String_String___init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueTable_String_String___to_String,
  0, // Table<<String,String>>.to_Value()
  0, // Object.to_ValueList()
  0, // Table<<String,String>>.to_ValueTable()
  0, // Object.type_info()
  0, // Table<<String,String>>.unpack(Value)
  (void*) (ROGUEM1) RogueTable_String_String___type_name,
  0, // Table<<String,String>>.init(Int32)
  0, // Table<<String,String>>.init(Table<<String,String>>)
  0, // Table<<String,String>>.add(Table<<String,String>>)
  0, // Table<<String,String>>.at(Int32)
  0, // Table<<String,String>>.clear()
  0, // Table<<String,String>>.cloned()
  0, // Table<<String,String>>.contains(String)
  0, // Table<<String,String>>.contains((Function(String)->Logical))
  0, // Table<<String,String>>.count((Function(Value)->Logical))
  0, // Table<<String,String>>.discard((Function(TableEntry<<String,String>>)->Logical))
  0, // Table<<String,String>>.entries(TableEntry<<String,String>>[])
  0, // Table<<String,String>>.is_empty()
  0, // Table<<String,String>>.find(String)
  0, // Table<<String,String>>.first()
  0, // Table<<String,String>>.first((Function(String)->Logical))
  (void*) (ROGUEM4) RogueTable_String_String___get__String,
  (void*) (ROGUEM0) RogueObject__init_object, // Array<<TableEntry<<String,String>>>>
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueArray_TableEntry_String_String____type_name,
  (void*) (ROGUEM1) RogueTableEntry_String_String___init_object, // TableEntry<<String,String>>
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueTableEntry_String_String___to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueTableEntry_String_String___type_name,
  (void*) (ROGUEM1) Rogue_Function_TableEntry_String_String__TableEntry_String_String__RETURNSLogical___init_object, // (Function(TableEntry<<String,String>>,TableEntry<<String,String>>)->Logical)
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) Rogue_Function_TableEntry_String_String__TableEntry_String_String__RETURNSLogical___type_name,
  0, // Reader<<Character>>.close() // Reader<<Character>>
  0, // Reader<<String>>.close() // Reader<<String>>
  (void*) (ROGUEM1) RogueConsole__init_object, // Console
  (void*) (ROGUEM1) RogueConsole__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueConsole__type_name,
  (void*) (ROGUEM1) RoguePrintWriter_output_buffer___close, // PrintWriter<<output_buffer>>
  (void*) (ROGUEM1) RogueConsoleErrorPrinter__init_object, // ConsoleErrorPrinter
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueConsoleErrorPrinter__type_name,
  (void*) (ROGUEM1) RoguePrimitiveWorkBuffer__init_object, // PrimitiveWorkBuffer
  (void*) (ROGUEM1) RogueStringBuilder__init,
  0, // StringBuilder.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueStringBuilder__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RoguePrimitiveWorkBuffer__type_name,
  (void*) (ROGUEM1) RogueMath__init_object, // Math
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueMath__type_name,
  (void*) (ROGUEM1) RogueFunction_221__init_object, // Function_221
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueFunction_221__type_name,
  (void*) (ROGUEM0) RogueFunction_221__call,
  (void*) (ROGUEM1) RogueSystem__init_object, // System
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueSystem__type_name,
  (void*) (ROGUEM1) RogueError__init_object, // Error
  (void*) (ROGUEM1) RogueException__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueException__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueError__type_name,
  (void*) (ROGUEM4) RogueException__init__String,
  0, // Exception.display()
  0, // Exception.format()
  (void*) (ROGUEM1) RogueError___throw,
  (void*) (ROGUEM1) RogueFile__init_object, // File
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueFile__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueFile__type_name,
  (void*) (ROGUEM1) RogueLineReader__init_object, // LineReader
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueLineReader__type_name,
  0, // Reader<<Byte>>.close() // Reader<<Byte>>
  (void*) (ROGUEM1) RogueFileWriter__init_object, // FileWriter
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueFileWriter__type_name,
  0, // Writer<<Byte>>.close() // Writer<<Byte>>
  (void*) (ROGUEM1) RogueScanner__init_object, // Scanner
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueScanner__type_name,
  (void*) (ROGUEM1) RogueJSONParser__init_object, // JSONParser
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueJSONParser__type_name,
  (void*) (ROGUEM1) RogueJSON__init_object, // JSON
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueJSON__type_name,
  (void*) (ROGUEM1) Rogue_Function_String_RETURNSString___init_object, // (Function(String)->String)
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) Rogue_Function_String_RETURNSString___type_name,
  (void*) (ROGUEM4) Rogue_Function_String_RETURNSString___call__String,
  (void*) (ROGUEM1) RogueFunction_283__init_object, // Function_283
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueFunction_283__type_name,
  (void*) (ROGUEM4) RogueFunction_283__call__String,
  (void*) (ROGUEM1) RogueRuntime__init_object, // Runtime
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueRuntime__type_name,
  (void*) (ROGUEM1) RogueWeakReference__init_object, // WeakReference
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueWeakReference__type_name,
  (void*) (ROGUEM1) RoguePrintWriterAdapter__init_object, // PrintWriterAdapter
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RoguePrintWriterAdapter__type_name,
  0, // PrintWriter<<buffer>>.close() // PrintWriter<<buffer>>
  (void*) (ROGUEM1) RogueLogicalValue__init_object, // LogicalValue
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Value.operator==(Object)
  (void*) (ROGUEM1) RogueLogicalValue__to_String,
  0, // Value.to_Value()
  0, // Value.to_ValueList()
  0, // Value.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueLogicalValue__type_name,
  0, // Value.add(Value)
  0, // Value.add(Value[])
  0, // Value.add_all(Value)
  0, // Value.clear()
  0, // Value.cloned()
  0, // Value.apply((Function(Value)->Value))
  (void*) (ROGUEM2) RogueValue__contains__String,
  (void*) (ROGUEM2) RogueValue__contains__Value,
  0, // Value.contains((Function(Value)->Logical))
  (void*) (ROGUEM3) RogueValue__count,
  0, // Value.count((Function(Value)->Logical))
  0, // Value.compressed()
  0, // Value.decompressed()
  0, // Value.decode_indexed(ValueIDLookupTable)
  0, // Value.encode_indexed(ValueIDTableBuilder)
  0, // Value.ensure_list(String)
  (void*) (ROGUEM4) RogueValue__ensure_table__String,
  0, // Value.first()
  0, // Value.first((Function(Value)->Logical))
  (void*) (ROGUEM5) RogueValue__get__Int32,
  (void*) (ROGUEM4) RogueValue__get__String,
  0, // Value.get((Function(Value)->Logical))
  0, // Value.insert(Value,Int32)
  0, // Value.insert(Value[],Int32)
  0, // Value.insert_all(Value,Int32)
  (void*) (ROGUEM6) RogueValue__is_collection,
  0, // Value.is_complex()
  0, // Value.is_int32()
  0, // Value.is_int64()
  0, // Value.is_list()
  (void*) (ROGUEM6) RogueLogicalValue__is_logical,
  (void*) (ROGUEM6) RogueValue__is_null,
  (void*) (ROGUEM6) RogueValue__is_non_null,
  (void*) (ROGUEM6) RogueValue__is_number,
  0, // Value.is_object()
  0, // Value.is_real64()
  (void*) (ROGUEM6) RogueValue__is_string,
  0, // Value.is_table()
  0, // Value.is_undefined()
  0, // Value.keys()
  0, // Value.last()
  0, // Value.last((Function(Value)->Logical))
  0, // Value.locate(Value)
  0, // Value.locate((Function(Value)->Logical))
  0, // Value.locate_last(Value)
  0, // Value.locate_last((Function(Value)->Logical))
  (void*) (ROGUEM2) RogueValue__operatorEQUALSEQUALS__Value,
  0, // Value.operator==(Byte)
  0, // Value.operator==(Character)
  0, // Value.operator==(Int32)
  0, // Value.operator==(Int64)
  0, // Value.operator==(Logical)
  0, // Value.operator==(Real64)
  0, // Value.operator==(Real32)
  0, // Value.operator==(String)
  0, // Value.operator<(Value)
  0, // Value.operator<(Byte)
  0, // Value.operator<(Character)
  0, // Value.operator<(Int32)
  0, // Value.operator<(Int64)
  0, // Value.operator<(Logical)
  0, // Value.operator<(Real64)
  0, // Value.operator<(Real32)
  0, // Value.operator<(Object)
  0, // Value.operator<(String)
  0, // Value.operator-()
  0, // Value.operator+(Value)
  0, // Value.operator-(Value)
  0, // Value.operator*(Value)
  0, // Value.operator/(Value)
  0, // Value.operator%(Value)
  0, // Value.operator+(Real64)
  0, // Value.operator-(Real64)
  0, // Value.operator*(Real64)
  0, // Value.operator/(Real64)
  0, // Value.operator%(Real64)
  0, // Value.operator+(String)
  0, // Value.operator*(String)
  (void*) (ROGUEM4) RogueValue__remove__Value,
  0, // Value.remove(String)
  0, // Value.remove((Function(Value)->Logical))
  0, // Value.remove_at(Int32)
  0, // Value.remove_first()
  0, // Value.remove_last()
  0, // Value.reserve(Int32)
  0, // Value.rest(Value)
  0, // Value.save(File,Logical,Logical)
  0, // Value.set(Int32,Value)
  (void*) (ROGUEM7) RogueValue__set__String_Value,
  0, // Value.sort((Function(Value,Value)->Logical))
  0, // Value.sorted((Function(Value,Value)->Logical))
  0, // Value.to_Byte()
  0, // Value.to_Character()
  (void*) (ROGUEM8) RogueLogicalValue__to_Int64,
  (void*) (ROGUEM3) RogueValue__to_Int32,
  (void*) (ROGUEM6) RogueLogicalValue__to_Logical,
  (void*) (ROGUEM9) RogueValue__to_Real64,
  0, // Value.to_Real32()
  0, // Value.to_Object()
  0, // Value.to_json(Int32)
  0, // Value.to_json(Logical,Logical)
  0, // Value.to_json(StringBuilder,Logical,Logical)
  (void*) (ROGUEM10) RogueLogicalValue__to_json__StringBuilder_Int32,
  (void*) (ROGUEM1) RogueReal64Value__init_object, // Real64Value
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Value.operator==(Object)
  (void*) (ROGUEM1) RogueReal64Value__to_String,
  0, // Value.to_Value()
  0, // Value.to_ValueList()
  0, // Value.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueReal64Value__type_name,
  0, // Value.add(Value)
  0, // Value.add(Value[])
  0, // Value.add_all(Value)
  0, // Value.clear()
  0, // Value.cloned()
  0, // Value.apply((Function(Value)->Value))
  (void*) (ROGUEM2) RogueValue__contains__String,
  (void*) (ROGUEM2) RogueValue__contains__Value,
  0, // Value.contains((Function(Value)->Logical))
  (void*) (ROGUEM3) RogueValue__count,
  0, // Value.count((Function(Value)->Logical))
  0, // Value.compressed()
  0, // Value.decompressed()
  0, // Value.decode_indexed(ValueIDLookupTable)
  0, // Value.encode_indexed(ValueIDTableBuilder)
  0, // Value.ensure_list(String)
  (void*) (ROGUEM4) RogueValue__ensure_table__String,
  0, // Value.first()
  0, // Value.first((Function(Value)->Logical))
  (void*) (ROGUEM5) RogueValue__get__Int32,
  (void*) (ROGUEM4) RogueValue__get__String,
  0, // Value.get((Function(Value)->Logical))
  0, // Value.insert(Value,Int32)
  0, // Value.insert(Value[],Int32)
  0, // Value.insert_all(Value,Int32)
  (void*) (ROGUEM6) RogueValue__is_collection,
  0, // Value.is_complex()
  0, // Value.is_int32()
  0, // Value.is_int64()
  0, // Value.is_list()
  (void*) (ROGUEM6) RogueValue__is_logical,
  (void*) (ROGUEM6) RogueValue__is_null,
  (void*) (ROGUEM6) RogueValue__is_non_null,
  (void*) (ROGUEM6) RogueReal64Value__is_number,
  0, // Value.is_object()
  0, // Real64Value.is_real64()
  (void*) (ROGUEM6) RogueValue__is_string,
  0, // Value.is_table()
  0, // Value.is_undefined()
  0, // Value.keys()
  0, // Value.last()
  0, // Value.last((Function(Value)->Logical))
  0, // Value.locate(Value)
  0, // Value.locate((Function(Value)->Logical))
  0, // Value.locate_last(Value)
  0, // Value.locate_last((Function(Value)->Logical))
  (void*) (ROGUEM2) RogueReal64Value__operatorEQUALSEQUALS__Value,
  0, // Value.operator==(Byte)
  0, // Value.operator==(Character)
  0, // Value.operator==(Int32)
  0, // Value.operator==(Int64)
  0, // Value.operator==(Logical)
  0, // Value.operator==(Real64)
  0, // Value.operator==(Real32)
  0, // Value.operator==(String)
  0, // Real64Value.operator<(Value)
  0, // Value.operator<(Byte)
  0, // Value.operator<(Character)
  0, // Value.operator<(Int32)
  0, // Value.operator<(Int64)
  0, // Value.operator<(Logical)
  0, // Value.operator<(Real64)
  0, // Value.operator<(Real32)
  0, // Value.operator<(Object)
  0, // Value.operator<(String)
  0, // Real64Value.operator-()
  0, // Real64Value.operator+(Value)
  0, // Real64Value.operator-(Value)
  0, // Real64Value.operator*(Value)
  0, // Real64Value.operator/(Value)
  0, // Real64Value.operator%(Value)
  0, // Real64Value.operator+(Real64)
  0, // Real64Value.operator-(Real64)
  0, // Real64Value.operator*(Real64)
  0, // Real64Value.operator/(Real64)
  0, // Real64Value.operator%(Real64)
  0, // Real64Value.operator+(String)
  0, // Real64Value.operator*(String)
  (void*) (ROGUEM4) RogueValue__remove__Value,
  0, // Value.remove(String)
  0, // Value.remove((Function(Value)->Logical))
  0, // Value.remove_at(Int32)
  0, // Value.remove_first()
  0, // Value.remove_last()
  0, // Value.reserve(Int32)
  0, // Value.rest(Value)
  0, // Value.save(File,Logical,Logical)
  0, // Value.set(Int32,Value)
  (void*) (ROGUEM7) RogueValue__set__String_Value,
  0, // Value.sort((Function(Value,Value)->Logical))
  0, // Value.sorted((Function(Value,Value)->Logical))
  0, // Value.to_Byte()
  0, // Value.to_Character()
  (void*) (ROGUEM8) RogueReal64Value__to_Int64,
  (void*) (ROGUEM3) RogueValue__to_Int32,
  (void*) (ROGUEM6) RogueValue__to_Logical,
  (void*) (ROGUEM9) RogueReal64Value__to_Real64,
  0, // Value.to_Real32()
  0, // Value.to_Object()
  0, // Value.to_json(Int32)
  0, // Value.to_json(Logical,Logical)
  0, // Value.to_json(StringBuilder,Logical,Logical)
  (void*) (ROGUEM10) RogueReal64Value__to_json__StringBuilder_Int32,
  (void*) (ROGUEM1) RogueNullValue__init_object, // NullValue
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Value.operator==(Object)
  (void*) (ROGUEM1) RogueNullValue__to_String,
  0, // Value.to_Value()
  0, // Value.to_ValueList()
  0, // Value.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueNullValue__type_name,
  0, // Value.add(Value)
  0, // Value.add(Value[])
  0, // Value.add_all(Value)
  0, // Value.clear()
  0, // Value.cloned()
  0, // Value.apply((Function(Value)->Value))
  (void*) (ROGUEM2) RogueValue__contains__String,
  (void*) (ROGUEM2) RogueValue__contains__Value,
  0, // Value.contains((Function(Value)->Logical))
  (void*) (ROGUEM3) RogueValue__count,
  0, // Value.count((Function(Value)->Logical))
  0, // Value.compressed()
  0, // Value.decompressed()
  0, // Value.decode_indexed(ValueIDLookupTable)
  0, // Value.encode_indexed(ValueIDTableBuilder)
  0, // Value.ensure_list(String)
  (void*) (ROGUEM4) RogueValue__ensure_table__String,
  0, // Value.first()
  0, // Value.first((Function(Value)->Logical))
  (void*) (ROGUEM5) RogueValue__get__Int32,
  (void*) (ROGUEM4) RogueValue__get__String,
  0, // Value.get((Function(Value)->Logical))
  0, // Value.insert(Value,Int32)
  0, // Value.insert(Value[],Int32)
  0, // Value.insert_all(Value,Int32)
  (void*) (ROGUEM6) RogueValue__is_collection,
  0, // Value.is_complex()
  0, // Value.is_int32()
  0, // Value.is_int64()
  0, // Value.is_list()
  (void*) (ROGUEM6) RogueValue__is_logical,
  (void*) (ROGUEM6) RogueNullValue__is_null,
  (void*) (ROGUEM6) RogueNullValue__is_non_null,
  (void*) (ROGUEM6) RogueValue__is_number,
  0, // Value.is_object()
  0, // Value.is_real64()
  (void*) (ROGUEM6) RogueValue__is_string,
  0, // Value.is_table()
  0, // Value.is_undefined()
  0, // Value.keys()
  0, // Value.last()
  0, // Value.last((Function(Value)->Logical))
  0, // Value.locate(Value)
  0, // Value.locate((Function(Value)->Logical))
  0, // Value.locate_last(Value)
  0, // Value.locate_last((Function(Value)->Logical))
  (void*) (ROGUEM2) RogueNullValue__operatorEQUALSEQUALS__Value,
  0, // Value.operator==(Byte)
  0, // Value.operator==(Character)
  0, // Value.operator==(Int32)
  0, // Value.operator==(Int64)
  0, // Value.operator==(Logical)
  0, // Value.operator==(Real64)
  0, // Value.operator==(Real32)
  0, // Value.operator==(String)
  0, // NullValue.operator<(Value)
  0, // Value.operator<(Byte)
  0, // Value.operator<(Character)
  0, // Value.operator<(Int32)
  0, // Value.operator<(Int64)
  0, // Value.operator<(Logical)
  0, // Value.operator<(Real64)
  0, // Value.operator<(Real32)
  0, // Value.operator<(Object)
  0, // Value.operator<(String)
  0, // Value.operator-()
  0, // Value.operator+(Value)
  0, // Value.operator-(Value)
  0, // Value.operator*(Value)
  0, // Value.operator/(Value)
  0, // Value.operator%(Value)
  0, // Value.operator+(Real64)
  0, // Value.operator-(Real64)
  0, // Value.operator*(Real64)
  0, // Value.operator/(Real64)
  0, // Value.operator%(Real64)
  0, // Value.operator+(String)
  0, // Value.operator*(String)
  (void*) (ROGUEM4) RogueValue__remove__Value,
  0, // Value.remove(String)
  0, // Value.remove((Function(Value)->Logical))
  0, // Value.remove_at(Int32)
  0, // Value.remove_first()
  0, // Value.remove_last()
  0, // Value.reserve(Int32)
  0, // Value.rest(Value)
  0, // Value.save(File,Logical,Logical)
  0, // Value.set(Int32,Value)
  (void*) (ROGUEM7) RogueValue__set__String_Value,
  0, // Value.sort((Function(Value,Value)->Logical))
  0, // Value.sorted((Function(Value,Value)->Logical))
  0, // Value.to_Byte()
  0, // Value.to_Character()
  (void*) (ROGUEM8) RogueNullValue__to_Int64,
  (void*) (ROGUEM3) RogueValue__to_Int32,
  (void*) (ROGUEM6) RogueValue__to_Logical,
  (void*) (ROGUEM9) RogueValue__to_Real64,
  0, // Value.to_Real32()
  0, // Value.to_Object()
  0, // Value.to_json(Int32)
  0, // Value.to_json(Logical,Logical)
  0, // Value.to_json(StringBuilder,Logical,Logical)
  (void*) (ROGUEM10) RogueNullValue__to_json__StringBuilder_Int32,
  (void*) (ROGUEM1) RogueStringValue__init_object, // StringValue
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Value.operator==(Object)
  (void*) (ROGUEM1) RogueStringValue__to_String,
  0, // Value.to_Value()
  0, // Value.to_ValueList()
  0, // Value.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueStringValue__type_name,
  0, // Value.add(Value)
  0, // Value.add(Value[])
  0, // Value.add_all(Value)
  0, // Value.clear()
  0, // Value.cloned()
  0, // Value.apply((Function(Value)->Value))
  (void*) (ROGUEM2) RogueValue__contains__String,
  (void*) (ROGUEM2) RogueValue__contains__Value,
  0, // Value.contains((Function(Value)->Logical))
  (void*) (ROGUEM3) RogueStringValue__count,
  0, // Value.count((Function(Value)->Logical))
  0, // Value.compressed()
  0, // Value.decompressed()
  0, // StringValue.decode_indexed(ValueIDLookupTable)
  0, // StringValue.encode_indexed(ValueIDTableBuilder)
  0, // Value.ensure_list(String)
  (void*) (ROGUEM4) RogueValue__ensure_table__String,
  0, // Value.first()
  0, // Value.first((Function(Value)->Logical))
  (void*) (ROGUEM5) RogueStringValue__get__Int32,
  (void*) (ROGUEM4) RogueValue__get__String,
  0, // Value.get((Function(Value)->Logical))
  0, // Value.insert(Value,Int32)
  0, // Value.insert(Value[],Int32)
  0, // Value.insert_all(Value,Int32)
  (void*) (ROGUEM6) RogueValue__is_collection,
  0, // Value.is_complex()
  0, // Value.is_int32()
  0, // Value.is_int64()
  0, // Value.is_list()
  (void*) (ROGUEM6) RogueValue__is_logical,
  (void*) (ROGUEM6) RogueValue__is_null,
  (void*) (ROGUEM6) RogueValue__is_non_null,
  (void*) (ROGUEM6) RogueValue__is_number,
  0, // Value.is_object()
  0, // Value.is_real64()
  (void*) (ROGUEM6) RogueStringValue__is_string,
  0, // Value.is_table()
  0, // Value.is_undefined()
  0, // Value.keys()
  0, // Value.last()
  0, // Value.last((Function(Value)->Logical))
  0, // Value.locate(Value)
  0, // Value.locate((Function(Value)->Logical))
  0, // Value.locate_last(Value)
  0, // Value.locate_last((Function(Value)->Logical))
  (void*) (ROGUEM2) RogueStringValue__operatorEQUALSEQUALS__Value,
  0, // Value.operator==(Byte)
  0, // Value.operator==(Character)
  0, // Value.operator==(Int32)
  0, // Value.operator==(Int64)
  0, // Value.operator==(Logical)
  0, // Value.operator==(Real64)
  0, // Value.operator==(Real32)
  0, // Value.operator==(String)
  0, // StringValue.operator<(Value)
  0, // Value.operator<(Byte)
  0, // Value.operator<(Character)
  0, // Value.operator<(Int32)
  0, // Value.operator<(Int64)
  0, // Value.operator<(Logical)
  0, // Value.operator<(Real64)
  0, // Value.operator<(Real32)
  0, // Value.operator<(Object)
  0, // Value.operator<(String)
  0, // Value.operator-()
  0, // StringValue.operator+(Value)
  0, // Value.operator-(Value)
  0, // StringValue.operator*(Value)
  0, // Value.operator/(Value)
  0, // Value.operator%(Value)
  0, // StringValue.operator+(Real64)
  0, // Value.operator-(Real64)
  0, // Value.operator*(Real64)
  0, // Value.operator/(Real64)
  0, // Value.operator%(Real64)
  0, // StringValue.operator+(String)
  0, // Value.operator*(String)
  (void*) (ROGUEM4) RogueValue__remove__Value,
  0, // Value.remove(String)
  0, // Value.remove((Function(Value)->Logical))
  0, // Value.remove_at(Int32)
  0, // Value.remove_first()
  0, // Value.remove_last()
  0, // Value.reserve(Int32)
  0, // Value.rest(Value)
  0, // Value.save(File,Logical,Logical)
  0, // Value.set(Int32,Value)
  (void*) (ROGUEM7) RogueValue__set__String_Value,
  0, // Value.sort((Function(Value,Value)->Logical))
  0, // Value.sorted((Function(Value,Value)->Logical))
  0, // Value.to_Byte()
  0, // StringValue.to_Character()
  (void*) (ROGUEM8) RogueStringValue__to_Int64,
  (void*) (ROGUEM3) RogueValue__to_Int32,
  (void*) (ROGUEM6) RogueStringValue__to_Logical,
  (void*) (ROGUEM9) RogueStringValue__to_Real64,
  0, // Value.to_Real32()
  0, // StringValue.to_Object()
  0, // Value.to_json(Int32)
  0, // Value.to_json(Logical,Logical)
  0, // Value.to_json(StringBuilder,Logical,Logical)
  (void*) (ROGUEM10) RogueStringValue__to_json__StringBuilder_Int32,
  (void*) (ROGUEM1) RogueUndefinedValue__init_object, // UndefinedValue
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Value.operator==(Object)
  (void*) (ROGUEM1) RogueUndefinedValue__to_String,
  0, // Value.to_Value()
  0, // UndefinedValue.to_ValueList()
  0, // Value.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueUndefinedValue__type_name,
  0, // Value.add(Value)
  0, // Value.add(Value[])
  0, // Value.add_all(Value)
  0, // Value.clear()
  0, // Value.cloned()
  0, // Value.apply((Function(Value)->Value))
  (void*) (ROGUEM2) RogueValue__contains__String,
  (void*) (ROGUEM2) RogueValue__contains__Value,
  0, // Value.contains((Function(Value)->Logical))
  (void*) (ROGUEM3) RogueValue__count,
  0, // Value.count((Function(Value)->Logical))
  0, // Value.compressed()
  0, // Value.decompressed()
  0, // Value.decode_indexed(ValueIDLookupTable)
  0, // Value.encode_indexed(ValueIDTableBuilder)
  0, // Value.ensure_list(String)
  (void*) (ROGUEM4) RogueValue__ensure_table__String,
  0, // Value.first()
  0, // Value.first((Function(Value)->Logical))
  (void*) (ROGUEM5) RogueValue__get__Int32,
  (void*) (ROGUEM4) RogueValue__get__String,
  0, // Value.get((Function(Value)->Logical))
  0, // Value.insert(Value,Int32)
  0, // Value.insert(Value[],Int32)
  0, // Value.insert_all(Value,Int32)
  (void*) (ROGUEM6) RogueValue__is_collection,
  0, // Value.is_complex()
  0, // Value.is_int32()
  0, // Value.is_int64()
  0, // Value.is_list()
  (void*) (ROGUEM6) RogueValue__is_logical,
  (void*) (ROGUEM6) RogueNullValue__is_null,
  (void*) (ROGUEM6) RogueNullValue__is_non_null,
  (void*) (ROGUEM6) RogueValue__is_number,
  0, // Value.is_object()
  0, // Value.is_real64()
  (void*) (ROGUEM6) RogueValue__is_string,
  0, // Value.is_table()
  0, // UndefinedValue.is_undefined()
  0, // Value.keys()
  0, // Value.last()
  0, // Value.last((Function(Value)->Logical))
  0, // Value.locate(Value)
  0, // Value.locate((Function(Value)->Logical))
  0, // Value.locate_last(Value)
  0, // Value.locate_last((Function(Value)->Logical))
  (void*) (ROGUEM2) RogueNullValue__operatorEQUALSEQUALS__Value,
  0, // Value.operator==(Byte)
  0, // Value.operator==(Character)
  0, // Value.operator==(Int32)
  0, // Value.operator==(Int64)
  0, // Value.operator==(Logical)
  0, // Value.operator==(Real64)
  0, // Value.operator==(Real32)
  0, // Value.operator==(String)
  0, // NullValue.operator<(Value)
  0, // Value.operator<(Byte)
  0, // Value.operator<(Character)
  0, // Value.operator<(Int32)
  0, // Value.operator<(Int64)
  0, // Value.operator<(Logical)
  0, // Value.operator<(Real64)
  0, // Value.operator<(Real32)
  0, // Value.operator<(Object)
  0, // Value.operator<(String)
  0, // Value.operator-()
  0, // Value.operator+(Value)
  0, // Value.operator-(Value)
  0, // Value.operator*(Value)
  0, // Value.operator/(Value)
  0, // Value.operator%(Value)
  0, // Value.operator+(Real64)
  0, // Value.operator-(Real64)
  0, // Value.operator*(Real64)
  0, // Value.operator/(Real64)
  0, // Value.operator%(Real64)
  0, // Value.operator+(String)
  0, // Value.operator*(String)
  (void*) (ROGUEM4) RogueValue__remove__Value,
  0, // Value.remove(String)
  0, // Value.remove((Function(Value)->Logical))
  0, // Value.remove_at(Int32)
  0, // Value.remove_first()
  0, // Value.remove_last()
  0, // Value.reserve(Int32)
  0, // Value.rest(Value)
  0, // Value.save(File,Logical,Logical)
  0, // Value.set(Int32,Value)
  (void*) (ROGUEM7) RogueValue__set__String_Value,
  0, // Value.sort((Function(Value,Value)->Logical))
  0, // Value.sorted((Function(Value,Value)->Logical))
  0, // Value.to_Byte()
  0, // Value.to_Character()
  (void*) (ROGUEM8) RogueNullValue__to_Int64,
  (void*) (ROGUEM3) RogueValue__to_Int32,
  (void*) (ROGUEM6) RogueValue__to_Logical,
  (void*) (ROGUEM9) RogueValue__to_Real64,
  0, // Value.to_Real32()
  0, // Value.to_Object()
  0, // Value.to_json(Int32)
  0, // Value.to_json(Logical,Logical)
  0, // Value.to_json(StringBuilder,Logical,Logical)
  (void*) (ROGUEM10) RogueNullValue__to_json__StringBuilder_Int32,
  (void*) (ROGUEM1) RogueOutOfBoundsError__init_object, // OutOfBoundsError
  (void*) (ROGUEM1) RogueException__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueException__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueOutOfBoundsError__type_name,
  (void*) (ROGUEM4) RogueOutOfBoundsError__init__String,
  0, // Exception.display()
  0, // Exception.format()
  (void*) (ROGUEM1) RogueOutOfBoundsError___throw,
  (void*) (ROGUEM1) RogueListRewriter_Character___init_object, // ListRewriter<<Character>>
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueListRewriter_Character___type_name,
  (void*) (ROGUEM1) RogueFunction_1184__init_object, // Function_1184
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueFunction_1184__type_name,
  (void*) (ROGUEM0) RogueFunction_1184__call,
  (void*) (ROGUEM1) RogueIOError__init_object, // IOError
  (void*) (ROGUEM1) RogueException__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueException__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueIOError__type_name,
  (void*) (ROGUEM4) RogueException__init__String,
  0, // Exception.display()
  0, // Exception.format()
  (void*) (ROGUEM1) RogueIOError___throw,
  (void*) (ROGUEM1) RogueFileReader__init_object, // FileReader
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueFileReader__type_name,
  (void*) (ROGUEM1) RogueUTF8Reader__init_object, // UTF8Reader
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueUTF8Reader__type_name,
  (void*) (ROGUEM1) RogueJSONParseError__init_object, // JSONParseError
  (void*) (ROGUEM1) RogueException__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueException__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueJSONParseError__type_name,
  (void*) (ROGUEM4) RogueJSONParseError__init__String,
  0, // Exception.display()
  0, // Exception.format()
  (void*) (ROGUEM1) RogueJSONParseError___throw,
  (void*) (ROGUEM1) RogueJSONParserBuffer__init_object, // JSONParserBuffer
  (void*) (ROGUEM1) RogueStringBuilder__init,
  0, // StringBuilder.hash_code()
  0, // Object.introspector()
  0, // Object.object_id()
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueStringBuilder__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueJSONParserBuffer__type_name,
  0, // SystemEnvironment.get(String) // SystemEnvironment

};

const int Rogue_type_info_table[] ={
  // allocator_index, dynamic_method_table_index, base_type_count, base_type_index[base_type_count],
  // global_property_count, global_property_name_indices[global_property_count], global_property_type_indices[global_property_count],
  // property_count, property_name_indices[property_count], property_type_indices[property_count] ...
  6,0,0,0,0,0,13,19,0,13,3,0,2,3,0,5,56,228,229,230,231,4,5,3,13,22,13,
  7,0,26,1,3,0,0,1,6,0,27,0,0,0,1,10,0,28,2,5,0,0,1,232,6,116,7,0,144,
  1,0,0,0,116,23,0,260,1,0,0,8,233,234,235,236,237,238,239,240,7,7,7,8,10,10,
  10,12,13,6,0,0,0,0,0,0,8,0,274,2,9,0,0,0,13,7,0,287,1,0,0,0,13,19,0,
  300,1,0,0,6,241,242,243,244,245,246,11,5,10,10,10,7,13,7,0,313,1,0,0,0,13,
  7,0,326,1,0,0,0,14,23,0,340,1,0,2,247,248,14,18,6,249,233,250,251,252,253,14,7,
  7,7,7,21,13,12,0,353,2,15,0,0,2,232,233,17,7,13,7,0,366,1,0,0,0,13,6,0,
  0,0,0,0,0,8,0,380,2,9,0,0,0,13,9,0,393,1,0,0,1,254,19,13,12,0,406,2,
  15,0,0,2,232,233,20,7,13,8,0,419,2,9,0,0,0,13,6,0,0,0,0,0,0,12,0,433,
  2,15,0,0,2,232,233,24,7,13,7,0,446,1,0,0,0,14,8,0,460,2,9,0,0,0,13,
  11,0,473,1,0,0,2,255,256,11,26,14,13,0,487,1,0,0,3,257,233,258,27,7,21,13,12,0,
  500,2,15,0,0,2,232,233,28,7,13,8,0,513,2,9,0,0,0,13,6,0,0,0,0,0,0,
  6,0,0,0,0,0,0,6,0,0,0,0,0,0,12,0,529,2,15,0,0,2,232,233,33,7,13,8,0,
  542,2,9,0,0,0,13,10,0,555,2,5,0,0,1,232,35,116,12,0,671,2,15,0,0,2,232,
  233,36,7,13,8,0,684,2,9,0,0,0,13,25,0,697,3,38,39,0,0,8,233,234,235,236,237,
  238,239,240,7,7,7,40,41,41,41,42,29,24,0,726,2,39,0,0,8,233,234,235,236,237,238,
  239,240,7,7,7,40,41,41,41,42,29,23,0,755,1,0,0,8,233,234,235,236,237,238,239,240,
  7,7,7,40,41,41,41,42,29,8,0,784,2,9,0,0,0,13,19,0,797,1,0,0,6,241,242,
  243,244,245,246,11,11,41,41,41,7,13,7,0,810,1,0,0,0,13,8,0,823,0,0,1,259,7,
  1,8,0,824,0,0,1,259,7,1,32,0,825,4,0,43,46,3,0,11,259,260,261,262,263,264,265,
  266,267,268,269,7,47,21,21,21,13,13,80,14,30,30,13,7,0,838,1,3,0,0,1,11,0,839,
  3,0,46,3,0,1,264,13,13,20,0,852,2,13,0,0,6,249,233,250,251,252,253,14,7,7,
  7,7,21,13,7,0,865,1,0,0,0,13,8,0,878,2,23,0,0,0,14,13,0,892,1,0,3,270,
  271,272,27,11,81,0,13,12,0,905,2,25,0,0,2,255,256,11,26,17,9,0,922,1,0,0,1,
  17,11,13,18,0,935,2,0,44,0,5,259,273,274,275,276,7,43,11,13,31,13,8,0,948,0,0,
  1,259,7,1,18,0,949,2,0,57,0,5,259,17,260,275,277,7,11,21,14,30,13,8,0,962,0,
  0,1,259,7,1,20,0,963,2,0,43,0,6,259,232,233,278,279,280,7,32,7,7,7,7,13,
  9,0,976,1,0,0,1,281,58,13,7,0,989,1,0,0,0,13,7,0,1002,1,0,0,0,14,8,0,1016,
  2,61,0,0,0,14,7,0,1030,1,0,0,0,13,11,0,1043,1,0,0,2,282,283,64,30,13,13,0,
  1056,3,0,66,3,0,2,284,275,57,13,13,7,0,1069,1,3,0,0,1,14,0,1070,2,5,0,2,
  285,286,67,67,1,242,21,116,10,0,1186,2,5,0,0,1,242,29,116,8,0,1302,2,5,0,0,0,
  116,12,0,1418,2,5,0,1,287,70,1,242,11,116,9,0,1534,3,69,5,0,0,0,116,13,0,1650,3,
  52,25,0,0,2,255,256,11,26,17,13,0,1667,1,0,0,3,288,289,290,32,7,7,13,10,0,1680,
  2,23,0,0,1,229,45,14,13,0,1694,3,52,25,0,0,2,255,256,11,26,17,20,0,1711,2,0,
  55,0,6,259,17,233,291,275,277,7,11,7,7,14,30,13,14,0,1724,2,0,43,0,3,259,292,
  274,7,55,83,13,13,0,1737,3,52,25,0,0,2,255,256,11,26,17,20,0,1754,2,13,0,0,6,
  249,233,250,251,252,253,14,7,7,7,7,21,13,10,0,1767,0,0,2,242,293,7,21,0,6,0,1767,
  0,0,0,1,10,0,1768,0,0,2,242,293,16,21,0,10,0,1768,0,0,2,242,293,31,21,0,
};

const int Rogue_object_size_table[84] =
{
  (int) sizeof(RogueObject),
  (int) sizeof(RogueClassGlobal),
  (int) sizeof(RogueClassPrintWriter_global_output_buffer_),
  (int) sizeof(RogueClassPrintWriter),
  (int) sizeof(RogueClassValueTable),
  (int) sizeof(RogueClassValue),
  (int) sizeof(RogueClassTable_String_Value_),
  (int) sizeof(RogueInt32),
  (int) sizeof(RogueArray),
  (int) sizeof(RogueArray),
  (int) sizeof(RogueClassTableEntry_String_Value_),
  (int) sizeof(RogueString),
  (int) sizeof(RogueClass_Function_TableEntry_String_Value__TableEntry_String_Value__RETURNSLogical_),
  (int) sizeof(RogueStringBuilder),
  (int) sizeof(RogueByte_List),
  (int) sizeof(RogueClassGenericList),
  (int) sizeof(RogueByte),
  (int) sizeof(RogueArray),
  (int) sizeof(RogueClassStringBuilderPool),
  (int) sizeof(RogueStringBuilder_List),
  (int) sizeof(RogueArray),
  (int) sizeof(RogueLogical),
  (int) sizeof(Rogue_Function____List),
  (int) sizeof(RogueClass_Function___),
  (int) sizeof(RogueArray),
  (int) sizeof(RogueException),
  (int) sizeof(RogueClassStackTrace),
  (int) sizeof(RogueString_List),
  (int) sizeof(RogueArray),
  (int) sizeof(RogueReal64),
  (int) sizeof(RogueInt64),
  (int) sizeof(RogueCharacter),
  (int) sizeof(RogueCharacter_List),
  (int) sizeof(RogueArray),
  (int) sizeof(RogueClassValueList),
  (int) sizeof(RogueValue_List),
  (int) sizeof(RogueArray),
  (int) sizeof(RogueClassStringConsolidationTable),
  (int) sizeof(RogueClassStringTable_String_),
  (int) sizeof(RogueClassTable_String_String_),
  (int) sizeof(RogueArray),
  (int) sizeof(RogueClassTableEntry_String_String_),
  (int) sizeof(RogueClass_Function_TableEntry_String_String__TableEntry_String_String__RETURNSLogical_),
  (int) sizeof(RogueClassReader_Character_),
  (int) sizeof(RogueClassReader_String_),
  (int) sizeof(RogueClassConsole),
  (int) sizeof(RogueClassPrintWriter_output_buffer_),
  (int) sizeof(RogueClassConsoleErrorPrinter),
  (int) sizeof(RogueClassPrimitiveWorkBuffer),
  (int) sizeof(RogueClassMath),
  (int) sizeof(RogueClassFunction_221),
  (int) sizeof(RogueClassSystem),
  (int) sizeof(RogueClassError),
  (int) sizeof(RogueClassFile),
  (int) sizeof(RogueClassLineReader),
  (int) sizeof(RogueClassReader_Byte_),
  (int) sizeof(RogueClassFileWriter),
  (int) sizeof(RogueClassWriter_Byte_),
  (int) sizeof(RogueClassScanner),
  (int) sizeof(RogueClassJSONParser),
  (int) sizeof(RogueClassJSON),
  (int) sizeof(RogueClass_Function_String_RETURNSString_),
  (int) sizeof(RogueClassFunction_283),
  (int) sizeof(RogueClassRuntime),
  (int) sizeof(RogueWeakReference),
  (int) sizeof(RogueClassPrintWriterAdapter),
  (int) sizeof(RogueClassPrintWriter_buffer_),
  (int) sizeof(RogueClassLogicalValue),
  (int) sizeof(RogueClassReal64Value),
  (int) sizeof(RogueClassNullValue),
  (int) sizeof(RogueClassStringValue),
  (int) sizeof(RogueClassUndefinedValue),
  (int) sizeof(RogueClassOutOfBoundsError),
  (int) sizeof(RogueClassListRewriter_Character_),
  (int) sizeof(RogueClassFunction_1184),
  (int) sizeof(RogueClassIOError),
  (int) sizeof(RogueClassFileReader),
  (int) sizeof(RogueClassUTF8Reader),
  (int) sizeof(RogueClassJSONParseError),
  (int) sizeof(RogueClassJSONParserBuffer),
  (int) sizeof(RogueOptionalInt32),
  (int) sizeof(RogueClassSystemEnvironment),
  (int) sizeof(RogueOptionalByte),
  (int) sizeof(RogueOptionalCharacter)
};

int Rogue_allocator_count = 1;
RogueAllocator Rogue_allocators[1];

int Rogue_type_count = 84;
RogueType Rogue_types[84];

RogueType* RogueTypeObject;
RogueType* RogueTypeGlobal;
RogueType* RogueTypePrintWriter_global_output_buffer_;
RogueType* RogueTypePrintWriter;
RogueType* RogueTypeValueTable;
RogueType* RogueTypeValue;
RogueType* RogueTypeTable_String_Value_;
RogueType* RogueTypeInt32;
RogueType* RogueTypeArray;
RogueType* RogueTypeTableEntry_String_Value_;
RogueType* RogueTypeString;
RogueType* RogueType_Function_TableEntry_String_Value__TableEntry_String_Value__RETURNSLogical_;
RogueType* RogueTypeStringBuilder;
RogueType* RogueTypeByte_List;
RogueType* RogueTypeGenericList;
RogueType* RogueTypeByte;
RogueType* RogueTypeStringBuilderPool;
RogueType* RogueTypeStringBuilder_List;
RogueType* RogueTypeLogical;
RogueType* RogueType_Function____List;
RogueType* RogueType_Function___;
RogueType* RogueTypeException;
RogueType* RogueTypeStackTrace;
RogueType* RogueTypeString_List;
RogueType* RogueTypeReal64;
RogueType* RogueTypeInt64;
RogueType* RogueTypeCharacter;
RogueType* RogueTypeCharacter_List;
RogueType* RogueTypeValueList;
RogueType* RogueTypeValue_List;
RogueType* RogueTypeStringConsolidationTable;
RogueType* RogueTypeStringTable_String_;
RogueType* RogueTypeTable_String_String_;
RogueType* RogueTypeTableEntry_String_String_;
RogueType* RogueType_Function_TableEntry_String_String__TableEntry_String_String__RETURNSLogical_;
RogueType* RogueTypeReader_Character_;
RogueType* RogueTypeReader_String_;
RogueType* RogueTypeConsole;
RogueType* RogueTypePrintWriter_output_buffer_;
RogueType* RogueTypeConsoleErrorPrinter;
RogueType* RogueTypePrimitiveWorkBuffer;
RogueType* RogueTypeMath;
RogueType* RogueTypeFunction_221;
RogueType* RogueTypeSystem;
RogueType* RogueTypeError;
RogueType* RogueTypeFile;
RogueType* RogueTypeLineReader;
RogueType* RogueTypeReader_Byte_;
RogueType* RogueTypeFileWriter;
RogueType* RogueTypeWriter_Byte_;
RogueType* RogueTypeScanner;
RogueType* RogueTypeJSONParser;
RogueType* RogueTypeJSON;
RogueType* RogueType_Function_String_RETURNSString_;
RogueType* RogueTypeFunction_283;
RogueType* RogueTypeRuntime;
RogueType* RogueTypeWeakReference;
RogueType* RogueTypePrintWriterAdapter;
RogueType* RogueTypePrintWriter_buffer_;
RogueType* RogueTypeLogicalValue;
RogueType* RogueTypeReal64Value;
RogueType* RogueTypeNullValue;
RogueType* RogueTypeStringValue;
RogueType* RogueTypeUndefinedValue;
RogueType* RogueTypeOutOfBoundsError;
RogueType* RogueTypeListRewriter_Character_;
RogueType* RogueTypeFunction_1184;
RogueType* RogueTypeIOError;
RogueType* RogueTypeFileReader;
RogueType* RogueTypeUTF8Reader;
RogueType* RogueTypeJSONParseError;
RogueType* RogueTypeJSONParserBuffer;
RogueType* RogueTypeOptionalInt32;
RogueType* RogueTypeSystemEnvironment;
RogueType* RogueTypeOptionalByte;
RogueType* RogueTypeOptionalCharacter;

int Rogue_literal_string_count = 294;
RogueString* Rogue_literal_strings[294];

RogueClassPrintWriter* RoguePrintWriter__create__Writer_Byte_( RogueClassWriter_Byte_* writer_0 )
{
  ROGUE_GC_CHECK;
  return (RogueClassPrintWriter*)(((RogueClassPrintWriter*)(((RogueClassPrintWriterAdapter*)(RoguePrintWriterAdapter__init__Writer_Byte_( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassPrintWriterAdapter*,ROGUE_CREATE_OBJECT(PrintWriterAdapter))), ((writer_0)) ))))));
}

RogueClassValue* RogueValue__create__Logical( RogueLogical value_0 )
{
  ROGUE_GC_CHECK;
  return (RogueClassValue*)(((RogueClassValue*)(((((value_0))) ? (ROGUE_ARG(RogueLogicalValue_true_value)) : ROGUE_ARG(RogueLogicalValue_false_value)))));
}

RogueClassValue* RogueValue__create__String( RogueString* value_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((void*)value_0) == ((void*)NULL)))
  {
    return (RogueClassValue*)(((RogueClassValue*)(((RogueClassNullValue*)ROGUE_SINGLETON(NullValue)))));
  }
  return (RogueClassValue*)(((RogueClassValue*)(((RogueClassStringValue*)(RogueStringValue__init__String( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassStringValue*,ROGUE_CREATE_OBJECT(StringValue))), value_0 ))))));
}

RogueLogical RogueOptionalValue__operator__Value( RogueClassValue* value_0 )
{
  ROGUE_GC_CHECK;
  return (RogueLogical)(((((((void*)value_0) != ((void*)NULL)) && ((Rogue_call_ROGUEM6( 45, value_0 ))))) && (((!((Rogue_call_ROGUEM6( 43, value_0 )))) || ((Rogue_call_ROGUEM6( 108, value_0 )))))));
}

RogueLogical RogueString__operatorEQUALSEQUALS__String_String( RogueString* a_0, RogueString* b_1 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((void*)a_0) == ((void*)NULL)))
  {
    return (RogueLogical)(((void*)b_1) == ((void*)NULL));
  }
  else
  {
    return (RogueLogical)(((RogueString__operatorEQUALSEQUALS__String( a_0, b_1 ))));
  }
}

RogueString* RogueString__operatorPLUS__String_String( RogueString* st_0, RogueString* value_1 )
{
  ROGUE_GC_CHECK;
  ROGUE_DEF_LOCAL_REF(RogueString*,_auto_14_2,(st_0));
  return (RogueString*)(((RogueString*)(RogueString__operatorPLUS__String( ROGUE_ARG(((((_auto_14_2))) ? (ROGUE_ARG(_auto_14_2)) : ROGUE_ARG(Rogue_literal_strings[1]))), value_1 ))));
}

RogueString* RogueString__operatorTIMES__String_Int32( RogueString* st_0, RogueInt32 value_1 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((void*)st_0) == ((void*)NULL)))
  {
    return (RogueString*)(((RogueString*)(NULL)));
  }
  return (RogueString*)(((RogueString*)(RogueString__times__Int32( st_0, value_1 ))));
}

void RogueStringBuilder__init_class()
{
  ROGUE_GC_CHECK;
  RogueStringBuilder_work_bytes = ((RogueByte_List*)(RogueByte_List__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueByte_List*,ROGUE_CREATE_OBJECT(Byte_List))) )));
  RogueStringBuilder_pool = ((RogueClassStringBuilderPool*)ROGUE_SINGLETON(StringBuilderPool));
}

RogueLogical RogueLogical__create__Int32( RogueInt32 value_0 )
{
  ROGUE_GC_CHECK;
  return (RogueLogical)(((value_0) != (0)));
}

RogueCharacter RogueCharacter__create__Int32( RogueInt32 value_0 )
{
  ROGUE_GC_CHECK;
  return (RogueCharacter)(((RogueCharacter)(value_0)));
}

RogueString* RogueConsole__input__String( RogueString* prompt_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(!!(prompt_0)))
  {
    RogueGlobal__flush( ROGUE_ARG(((RogueClassGlobal*)(RogueGlobal__print__String( ((RogueClassGlobal*)ROGUE_SINGLETON(Global)), prompt_0 )))) );
  }
  char st[4096];
  if (!fgets( st, 4096, stdin ))
  {
    return RogueString_create_from_utf8( st, 0 );
  }
  // discard \n
  int len = strlen( st );
  if (len) st[--len] = 0;
  else st[0] = 0;
  return RogueString_create_from_utf8( st, len );

}

RogueInt32 RogueMath__max__Int32_Int32( RogueInt32 a_0, RogueInt32 b_1 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((a_0) >= (b_1))))
  {
    return (RogueInt32)(a_0);
  }
  else
  {
    return (RogueInt32)(b_1);
  }
}

RogueInt64 RogueMath__mod__Int64_Int64( RogueInt64 a_0, RogueInt64 b_1 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((((!(!!(a_0))) && (!(!!(b_1))))) || (((b_1) == (1LL))))))
  {
    return (RogueInt64)(0LL);
  }
  RogueInt64 r_2 = (((RogueInt64)(a_0 % b_1)));
  if (ROGUE_COND(((((a_0) ^ (b_1))) < (0LL))))
  {
    if (ROGUE_COND(!!(r_2)))
    {
      return (RogueInt64)(((r_2) + (b_1)));
    }
    else
    {
      return (RogueInt64)(0LL);
    }
  }
  else
  {
    return (RogueInt64)(r_2);
  }
}

RogueReal64 RogueMath__mod__Real64_Real64( RogueReal64 a_0, RogueReal64 b_1 )
{
  ROGUE_GC_CHECK;
  RogueReal64 q_2 = (((a_0) / (b_1)));
  return (RogueReal64)(((a_0) - (((floor((double)q_2)) * (b_1)))));
}

void RogueSystem__exit__Int32( RogueInt32 result_code_0 )
{
  ROGUE_GC_CHECK;
  Rogue_quit();
  exit( result_code_0 );

}

RogueString* RogueSystem__os()
{
  ROGUE_GC_CHECK;
  ROGUE_DEF_LOCAL_REF(RogueString*,result_0,(Rogue_literal_strings[63]));
#if __APPLE__
    #if TARGET_IPHONE_SIMULATOR || TARGET_OS_IPHONE

  result_0 = ((RogueString*)(Rogue_literal_strings[64]));
    #else

  result_0 = ((RogueString*)(Rogue_literal_strings[65]));
    #endif
#elif _WIN64 || _WIN32

  result_0 = ((RogueString*)(Rogue_literal_strings[66]));
#elif ANDROID

  result_0 = ((RogueString*)(Rogue_literal_strings[67]));
#elif __CYGWIN__

  result_0 = ((RogueString*)(Rogue_literal_strings[68]));
#elif defined(EMSCRIPTEN)

  result_0 = ((RogueString*)(Rogue_literal_strings[69]));
#endif

  return (RogueString*)(result_0);
}

RogueInt32 RogueSystem__run__String( RogueString* command_0 )
{
  ROGUE_GC_CHECK;
  RogueInt32 return_val_1 = (0);
  return_val_1 = system( (char*)command_0->utf8 );

  if (ROGUE_COND(((return_val_1) == (-1))))
  {
    throw ((RogueClassError*)Rogue_call_ROGUEM1( 16, ROGUE_ARG(((RogueClassError*)(((RogueException*)Rogue_call_ROGUEM4( 13, ROGUE_ARG(((RogueException*)ROGUE_CREATE_REF(RogueClassError*,ROGUE_CREATE_OBJECT(Error)))), Rogue_literal_strings[70] ))))) ));
  }
  return (RogueInt32)(return_val_1);
}

void RogueSystem__sync_storage()
{
  ROGUE_GC_CHECK;
}

void RogueSystem__init_class()
{
  RogueSystem_command_line_arguments = ((RogueString_List*)(RogueString_List__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueString_List*,ROGUE_CREATE_OBJECT(String_List))) )));
}

RogueString* RogueFile__absolute_filepath__String( RogueString* filepath_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(!(!!(filepath_0))))
  {
    return (RogueString*)(((RogueString*)(NULL)));
  }
  if (ROGUE_COND(!((RogueFile__exists__String( filepath_0 )))))
  {
    ROGUE_DEF_LOCAL_REF(RogueString*,parent_1,((RogueFile__folder__String( filepath_0 ))));
    if (ROGUE_COND(((parent_1->character_count) == (0))))
    {
      parent_1 = ((RogueString*)(Rogue_literal_strings[2]));
    }
    return (RogueString*)(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], ROGUE_ARG((RogueFile__absolute_filepath__String( parent_1 ))) ))) )))), Rogue_literal_strings[3] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], ROGUE_ARG((RogueFile__filename__String( filepath_0 ))) ))) )))) ))));
  }
  ROGUE_DEF_LOCAL_REF(RogueString*,return_value_2,0);
#if defined(_WIN32)
  {
    char long_name[PATH_MAX+4];
    char full_name[PATH_MAX+4];
    strcpy_s( long_name, PATH_MAX+4, (char*)filepath_0->utf8 );
    if (GetFullPathName(long_name, PATH_MAX+4, full_name, 0) != 0)
    {
      return_value_2 = RogueString_create_from_utf8( full_name, -1 );
    }
  }
#else
  {
    int original_dir_fd;
    int new_dir_fd;
    char filename[PATH_MAX];
    char c_filepath[ PATH_MAX ];
    bool is_folder;
    is_folder = RogueFile__is_folder__String( filepath_0 );
    int len = filepath_0->byte_count;
    if (len >= PATH_MAX) len = PATH_MAX - 1;
    memcpy( c_filepath, (char*)filepath_0->utf8, len );
    c_filepath[len] = 0;
    // A way to get back to the starting folder when finished.
    original_dir_fd = open( ".", O_RDONLY );
    if (is_folder)
    {
      filename[0] = 0;
    }
    else
    {
      // fchdir only works with a path, not a path+filename (c_filepath).
      // Copy out the filename and null terminate the filepath to be just a path.
      int i = (int) strlen( c_filepath ) - 1;
      while (i >= 0 && c_filepath[i] != '/') --i;
      strcpy( filename, c_filepath+i+1 );
      if (i == -1) strcpy( c_filepath, "." );
      else         c_filepath[i] = 0;
    }
    new_dir_fd = open( c_filepath, O_RDONLY );
    do
    {
      if (original_dir_fd >= 0 && new_dir_fd >= 0)
      {
        int r = fchdir( new_dir_fd );
        if ( r != 0 ) break;
        char * r2 = getcwd( c_filepath, PATH_MAX );
        if ( r2 == 0 ) break;
        if ( !is_folder )
        {
          strcat( c_filepath, "/" );
          strcat( c_filepath, filename );
        }
        r = fchdir( original_dir_fd );
        if ( r != 0 ) break;
      }
      return_value_2 = RogueString_create_from_utf8( c_filepath, -1 );
    } while (false);
    if (original_dir_fd >= 0) close( original_dir_fd );
    if (new_dir_fd >= 0) close( new_dir_fd );
  }
#endif

  if (ROGUE_COND(((void*)return_value_2) == ((void*)NULL)))
  {
    throw ((RogueClassIOError*)(RogueIOError___throw( ROGUE_ARG(((RogueClassIOError*)(((RogueException*)Rogue_call_ROGUEM4( 13, ROGUE_ARG(((RogueException*)ROGUE_CREATE_REF(RogueClassIOError*,ROGUE_CREATE_OBJECT(IOError)))), Rogue_literal_strings[14] ))))) )));
  }
  return (RogueString*)(return_value_2);
}

RogueLogical RogueFile__create_folder__String( RogueString* filepath_0 )
{
  ROGUE_GC_CHECK;
  filepath_0 = ((RogueString*)((RogueFile__absolute_filepath__String( filepath_0 ))));
  if (ROGUE_COND((RogueFile__exists__String( filepath_0 ))))
  {
    return (RogueLogical)((RogueFile__is_folder__String( filepath_0 )));
  }
  ROGUE_DEF_LOCAL_REF(RogueString*,parent_1,((RogueFile__folder__String( filepath_0 ))));
  if (ROGUE_COND(!((RogueFile__create_folder__String( parent_1 )))))
  {
    return (RogueLogical)(false);
  }
#if defined(ROGUE_PLATFORM_WINDOWS)
    return (0 == mkdir((char*)filepath_0->utf8));
#else
    return (0 == mkdir((char*)filepath_0->utf8, 0777));
#endif

}

RogueLogical RogueFile__delete__String( RogueString* filepath_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(!(!!(filepath_0))))
  {
    return (RogueLogical)(false);
  }
  return (RogueLogical)(((0) == (((RogueInt32)(unlink( (const char*) filepath_0->utf8 ))))));
}

RogueLogical RogueFile__exists__String( RogueString* filepath_0 )
{
  ROGUE_GC_CHECK;
  if ( !filepath_0 ) return false;
  struct stat s;
  return (stat((char*)filepath_0->utf8, &s) == 0);

}

RogueString* RogueFile__filename__String( RogueString* filepath_0 )
{
  ROGUE_GC_CHECK;
  RogueOptionalInt32 i_1 = (((RogueString__locate_last__Character_OptionalInt32( filepath_0, (RogueCharacter)'/', (RogueOptionalInt32__create()) ))));
  if (ROGUE_COND(!(i_1.exists)))
  {
    i_1 = ((RogueOptionalInt32)(((RogueString__locate_last__Character_OptionalInt32( filepath_0, (RogueCharacter)'\\', (RogueOptionalInt32__create()) )))));
  }
  if (ROGUE_COND(!(i_1.exists)))
  {
    return (RogueString*)(filepath_0);
  }
  return (RogueString*)(((RogueString*)(RogueString__from__Int32( filepath_0, ROGUE_ARG(((i_1.value) + (1))) ))));
}

RogueString* RogueFile__folder__String( RogueString* filepath_0 )
{
  ROGUE_GC_CHECK;
  RogueOptionalInt32 i_1 = (((RogueString__locate_last__Character_OptionalInt32( filepath_0, (RogueCharacter)'/', (RogueOptionalInt32__create()) ))));
  if (ROGUE_COND(!(i_1.exists)))
  {
    i_1 = ((RogueOptionalInt32)(((RogueString__locate_last__Character_OptionalInt32( filepath_0, (RogueCharacter)'\\', (RogueOptionalInt32__create()) )))));
  }
  if (ROGUE_COND(!(i_1.exists)))
  {
    return (RogueString*)(Rogue_literal_strings[0]);
  }
  return (RogueString*)(((RogueString*)(RogueString__from__Int32_Int32( filepath_0, 0, ROGUE_ARG(((i_1.value) - (1))) ))));
}

RogueLogical RogueFile__is_folder__String( RogueString* filepath_0 )
{
  ROGUE_GC_CHECK;
  if ( !filepath_0 ) return false;
#if defined(_WIN32)
    struct stat s;
    if (stat((char*)filepath_0->utf8, &s) != 0) return false;
    return (s.st_mode & S_IFMT) == S_IFDIR;
#else
    DIR* dir = opendir( (char*)filepath_0->utf8 );
    if ( !dir ) return 0;
    closedir( dir );
    return 1;
#endif

}

RogueLogical RogueFile__is_newer_than__String_String( RogueString* filepath_0, RogueString* other_filepath_1 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(!((RogueFile__exists__String( filepath_0 )))))
  {
    return (RogueLogical)(false);
  }
  if (ROGUE_COND(!((RogueFile__exists__String( other_filepath_1 )))))
  {
    return (RogueLogical)(true);
  }
  return (RogueLogical)((((RogueFile__timestamp__String( filepath_0 ))) > ((RogueFile__timestamp__String( other_filepath_1 )))));
}

RogueString* RogueFile__load_as_string__String( RogueString* filepath_0 )
{
  ROGUE_GC_CHECK;
  RogueInt64 count_1 = ((RogueFile__size__String( filepath_0 )));
  ROGUE_DEF_LOCAL_REF(RogueStringBuilder*,buffer_2,(((RogueStringBuilder*)(RogueStringBuilder__init__Int32( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))), ROGUE_ARG(((RogueInt32)(count_1))) )))));
  ROGUE_DEF_LOCAL_REF(RogueClassFileReader*,infile_3,((RogueFile__reader__String( filepath_0 ))));
  {
    RogueInt32 _auto_227_5 = (1);
    RogueInt64 _auto_228_6 = (count_1);
    for (;ROGUE_COND(((((RogueInt64)(_auto_227_5))) <= (_auto_228_6)));++_auto_227_5)
    {
      ROGUE_GC_CHECK;
      RogueByte b_4 = (((RogueFileReader__read( infile_3 ))));
      RogueByte_List__add__Byte( ROGUE_ARG(buffer_2->utf8), b_4 );
      if (ROGUE_COND(((((((RogueInt32)(b_4))) & (192))) != (128))))
      {
        ++buffer_2->count;
      }
    }
  }
  RogueFileReader__close( infile_3 );
  return (RogueString*)(((RogueString*)(RogueStringBuilder__to_String( buffer_2 ))));
}

RogueLogical RogueFile__matches_wildcard_pattern__String_String( RogueString* filepath_0, RogueString* pattern_1 )
{
  ROGUE_GC_CHECK;
  RogueInt32 last_wildcard_2 = (-1);
  {
    ROGUE_DEF_LOCAL_REF(RogueString*,_auto_1228_0,(pattern_1));
    RogueInt32 i_0 = (((_auto_1228_0->character_count) - (1)));
    for (;ROGUE_COND(((i_0) >= (0)));--i_0)
    {
      ROGUE_GC_CHECK;
      RogueCharacter ch_0 = (RogueString_character_at(_auto_1228_0,i_0));
      if (ROGUE_COND(((((ch_0) == ((RogueCharacter)'*'))) || (((ch_0) == ((RogueCharacter)'?'))))))
      {
        last_wildcard_2 = ((RogueInt32)(i_0));
        goto _auto_1229;
      }
    }
  }
  _auto_1229:;
  if (ROGUE_COND(((last_wildcard_2) != (-1))))
  {
    RogueInt32 end_count_3 = (((pattern_1->character_count) - (((last_wildcard_2) + (1)))));
    if (ROGUE_COND(((end_count_3) > (0))))
    {
      if (ROGUE_COND(((end_count_3) > (filepath_0->character_count))))
      {
        return (RogueLogical)(false);
      }
      RogueInt32 i_4 = (((last_wildcard_2) + (1)));
      {
        ROGUE_DEF_LOCAL_REF(RogueString*,_auto_1230_0,(filepath_0));
        RogueInt32 _auto_1231_0 = (((filepath_0->character_count) - (end_count_3)));
        RogueInt32 _auto_1232_0 = (((_auto_1230_0->character_count) - (1)));
        for (;ROGUE_COND(((_auto_1231_0) <= (_auto_1232_0)));++_auto_1231_0)
        {
          ROGUE_GC_CHECK;
          RogueCharacter ch_0 = (RogueString_character_at(_auto_1230_0,_auto_1231_0));
          if (ROGUE_COND(((ch_0) != (RogueString_character_at(pattern_1,i_4)))))
          {
            return (RogueLogical)(false);
          }
          ++i_4;
        }
      }
    }
  }
  return (RogueLogical)((RogueFile___matches_wildcard_pattern__String_Int32_Int32_String_Int32_Int32( filepath_0, 0, ROGUE_ARG(filepath_0->character_count), pattern_1, 0, ROGUE_ARG(pattern_1->character_count) )));
}

RogueLogical RogueFile___matches_wildcard_pattern__String_Int32_Int32_String_Int32_Int32( RogueString* filepath_0, RogueInt32 f0_1, RogueInt32 fcount_2, RogueString* pattern_3, RogueInt32 p0_4, RogueInt32 pcount_5 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((pcount_5) == (0))))
  {
    return (RogueLogical)(((fcount_2) == (0)));
  }
  ROGUE_DEF_LOCAL_REF(RogueString*,remaining_pattern_6,(pattern_3));
  RogueInt32 r0_7 = (((p0_4) + (1)));
  RogueInt32 rcount_8 = (((pcount_5) - (1)));
  RogueCharacter ch_9 = (RogueString_character_at(pattern_3,p0_4));
  switch (ch_9)
  {
    case (RogueCharacter)'*':
    {
      if (ROGUE_COND(((!!(rcount_8)) && (((RogueString_character_at(remaining_pattern_6,r0_7)) == ((RogueCharacter)'*'))))))
      {
        ++r0_7;
        --rcount_8;
        {
          RogueInt32 n_10 = (0);
          RogueInt32 _auto_229_11 = (fcount_2);
          for (;ROGUE_COND(((n_10) <= (_auto_229_11)));++n_10)
          {
            ROGUE_GC_CHECK;
            if (ROGUE_COND((RogueFile___matches_wildcard_pattern__String_Int32_Int32_String_Int32_Int32( filepath_0, ROGUE_ARG(((f0_1) + (n_10))), ROGUE_ARG(((fcount_2) - (n_10))), remaining_pattern_6, r0_7, rcount_8 ))))
            {
              return (RogueLogical)(true);
            }
          }
        }
      }
      else
      {
        {
          RogueInt32 n_12 = (0);
          RogueInt32 _auto_230_13 = (fcount_2);
          for (;ROGUE_COND(((n_12) < (_auto_230_13)));++n_12)
          {
            ROGUE_GC_CHECK;
            ch_9 = ((RogueCharacter)(RogueString_character_at(filepath_0,((f0_1) + (n_12)))));
            if (ROGUE_COND((RogueFile___matches_wildcard_pattern__String_Int32_Int32_String_Int32_Int32( filepath_0, ROGUE_ARG(((f0_1) + (n_12))), ROGUE_ARG(((fcount_2) - (n_12))), remaining_pattern_6, r0_7, rcount_8 ))))
            {
              return (RogueLogical)(true);
            }
            if (ROGUE_COND(((((ch_9) == ((RogueCharacter)'/'))) || (((ch_9) == ((RogueCharacter)'\\'))))))
            {
              return (RogueLogical)(false);
            }
          }
        }
        return (RogueLogical)((RogueFile___matches_wildcard_pattern__String_Int32_Int32_String_Int32_Int32( Rogue_literal_strings[0], 0, 0, remaining_pattern_6, r0_7, rcount_8 )));
      }
      break;
    }
    case (RogueCharacter)'?':
    {
      if (ROGUE_COND(((fcount_2) == (0))))
      {
        return (RogueLogical)(false);
      }
      ch_9 = ((RogueCharacter)(RogueString_character_at(filepath_0,f0_1)));
      if (ROGUE_COND(((((ch_9) == ((RogueCharacter)'/'))) || (((ch_9) == ((RogueCharacter)'\\'))))))
      {
        return (RogueLogical)(false);
      }
      return (RogueLogical)((RogueFile___matches_wildcard_pattern__String_Int32_Int32_String_Int32_Int32( filepath_0, ROGUE_ARG(((f0_1) + (1))), ROGUE_ARG(((fcount_2) - (1))), remaining_pattern_6, r0_7, rcount_8 )));
    }
    default:
    {
      if (ROGUE_COND(((fcount_2) == (0))))
      {
        return (RogueLogical)(false);
      }
      if (ROGUE_COND(((ch_9) == (RogueString_character_at(filepath_0,f0_1)))))
      {
        return (RogueLogical)((RogueFile___matches_wildcard_pattern__String_Int32_Int32_String_Int32_Int32( filepath_0, ROGUE_ARG(((f0_1) + (1))), ROGUE_ARG(((fcount_2) - (1))), remaining_pattern_6, r0_7, rcount_8 )));
      }
    }
  }
  return (RogueLogical)(false);
}

RogueClassFileReader* RogueFile__reader__String( RogueString* filepath_0 )
{
  ROGUE_GC_CHECK;
  return (RogueClassFileReader*)(((RogueClassFileReader*)(RogueFileReader__init__String( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassFileReader*,ROGUE_CREATE_OBJECT(FileReader))), filepath_0 ))));
}

RogueLogical RogueFile__save__String_String( RogueString* filepath_0, RogueString* data_1 )
{
  ROGUE_GC_CHECK;
  ROGUE_DEF_LOCAL_REF(RogueClassFileWriter*,outfile_2,((RogueFile__writer__String( filepath_0 ))));
  RogueFileWriter__write__String( outfile_2, data_1 );
  RogueFileWriter__close( outfile_2 );
  return (RogueLogical)(!(outfile_2->error));
}

RogueInt64 RogueFile__size__String( RogueString* filepath_0 )
{
  ROGUE_GC_CHECK;
  if ( !filepath_0 ) return 0;
  FILE* fp = fopen( (char*)filepath_0->utf8, "rb" );
  if ( !fp ) return 0;
  fseek( fp, 0, SEEK_END );
  RogueInt64 size = (RogueInt64) ftell( fp );
  fclose( fp );
  return size;

}

RogueReal64 RogueFile__timestamp__String( RogueString* filepath_0 )
{
  ROGUE_GC_CHECK;
#if defined(_WIN32)
    HANDLE handle = CreateFile( (const char*)filepath_0->utf8, 0, 0, NULL, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, NULL );
    if (handle != INVALID_HANDLE_VALUE)
    {
      BY_HANDLE_FILE_INFORMATION info;
      if (GetFileInformationByHandle( handle, &info ))
      {
        RogueInt64 result = info.ftLastWriteTime.dwHighDateTime;
        result <<= 32;
        result |= info.ftLastWriteTime.dwLowDateTime;
        result /= 10000; // convert from Crazyseconds to Milliseconds
        result -= 11644473600000;  // base on Jan 1, 1970 instead of Jan 1, 1601 (?!)
        CloseHandle(handle);
        return result / 1000.0;
      }
      CloseHandle(handle);
    }
#elif defined(ROGUE_PLATFORM_UNIX_COMPATIBLE)
    int file_id = open( (const char*)filepath_0->utf8, O_RDONLY );
    if (file_id >= 0)
    {
      struct stat info;
      if (0 == fstat(file_id, &info))
      {
#if defined(__APPLE__)
        RogueInt64 result = info.st_mtimespec.tv_sec;
        result *= 1000000000;
        result += info.st_mtimespec.tv_nsec;
        result /= 1000000;  // convert to milliseconds
#else
        RogueInt64 result = (RogueInt64) info.st_mtime;
        result *= 1000;  // convert to ms
#endif
        close(file_id);
        return result / 1000.0;
      }
      close(file_id);
    }
#else
# error Must define File.timestamp() for this OS.
#endif
  return 0.0;

}

RogueClassFileWriter* RogueFile__writer__String( RogueString* filepath_0 )
{
  ROGUE_GC_CHECK;
  return (RogueClassFileWriter*)(((RogueClassFileWriter*)(RogueFileWriter__init__String_Logical( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassFileWriter*,ROGUE_CREATE_OBJECT(FileWriter))), filepath_0, false ))));
}

RogueClassValue* RogueJSON__load_table__File( RogueClassFile* file_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((!(!!(file_0))) || (!((RogueFile__exists__String( ROGUE_ARG(file_0->filepath) )))))))
  {
    ROGUE_DEF_LOCAL_REF(RogueClassValueTable*,_auto_1261_0,(((RogueClassValueTable*)(RogueValueTable__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassValueTable*,ROGUE_CREATE_OBJECT(ValueTable))) )))));
    {
    }
    return (RogueClassValue*)(((RogueClassValue*)(_auto_1261_0)));
  }
  return (RogueClassValue*)(((RogueClassValue*)((RogueJSON__parse_table__String( ROGUE_ARG((RogueFile__load_as_string__String( ROGUE_ARG(file_0->filepath) ))) )))));
}

RogueClassValue* RogueJSON__parse__String( RogueString* json_0 )
{
  ROGUE_GC_CHECK;
  ROGUE_TRY
  {
    return (RogueClassValue*)(((RogueClassValue*)(RogueJSONParser__parse_value( ROGUE_ARG(((RogueClassJSONParser*)(RogueJSONParser__init__String( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassJSONParser*,ROGUE_CREATE_OBJECT(JSONParser))), json_0 )))) ))));
  }
  ROGUE_CATCH_NO_VAR(RogueClassJSONParseError)
  {
    return (RogueClassValue*)(((RogueClassValue*)(((RogueClassUndefinedValue*)ROGUE_SINGLETON(UndefinedValue)))));
  }
  ROGUE_END_TRY
}

RogueClassValueTable* RogueJSON__parse_table__String( RogueString* json_0 )
{
  ROGUE_GC_CHECK;
  ROGUE_DEF_LOCAL_REF(RogueClassValueTable*,table_1,(((RogueClassValueTable*)(RogueObject_as((RogueJSON__parse__String( json_0 )),RogueTypeValueTable)))));
  if (ROGUE_COND((RogueOptionalValue__operator__Value( ROGUE_ARG(((RogueClassValue*)(table_1))) ))))
  {
    return (RogueClassValueTable*)(table_1);
  }
  return (RogueClassValueTable*)(((RogueClassValueTable*)(RogueValueTable__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassValueTable*,ROGUE_CREATE_OBJECT(ValueTable))) ))));
}

void RogueRuntime__init_class()
{
  ROGUE_GC_CHECK;
  ROGUE_DEF_LOCAL_REF(RogueString*,value_0,(((RogueString*)(RogueSystemEnvironment__get__String( RogueSystem_environment, Rogue_literal_strings[127] )))));
  if (ROGUE_COND(((void*)value_0) != ((void*)NULL)))
  {
    RogueReal64 n_1 = (strtod( (char*)value_0->utf8, 0 ));
    if (ROGUE_COND(((((RogueString__ends_with__Character( value_0, (RogueCharacter)'M' )))) || (((RogueString__ends_with__String( value_0, Rogue_literal_strings[128] )))))))
    {
      n_1 *= 1048576.0;
    }
    else if (ROGUE_COND(((((RogueString__ends_with__Character( value_0, (RogueCharacter)'K' )))) || (((RogueString__ends_with__String( value_0, Rogue_literal_strings[129] )))))))
    {
      n_1 *= 1024.0;
    }
    RogueRuntime__set_gc_threshold__Int32( ROGUE_ARG(((RogueInt32)(n_1))) );
  }
}

void RogueRuntime__set_gc_threshold__Int32( RogueInt32 value_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((value_0) <= (0))))
  {
    value_0 = ((RogueInt32)(2147483647));
  }
  Rogue_gc_threshold = value_0;

}

void RogueLogicalValue__init_class()
{
  ROGUE_GC_CHECK;
  RogueLogicalValue_true_value = ((RogueClassLogicalValue*)(RogueLogicalValue__init__Logical( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassLogicalValue*,ROGUE_CREATE_OBJECT(LogicalValue))), true )));
  RogueLogicalValue_false_value = ((RogueClassLogicalValue*)(RogueLogicalValue__init__Logical( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassLogicalValue*,ROGUE_CREATE_OBJECT(LogicalValue))), false )));
}

RogueStringBuilder* RogueStringValue__to_json__String_StringBuilder_Int32( RogueString* value_0, RogueStringBuilder* buffer_1, RogueInt32 flags_2 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(!!(value_0)))
  {
    RogueStringBuilder__print__Character_Logical( buffer_1, (RogueCharacter)'"', true );
    {
      ROGUE_DEF_LOCAL_REF(RogueString*,_auto_1256_0,(value_0));
      RogueInt32 _auto_1257_0 = (0);
      RogueInt32 _auto_1258_0 = (((_auto_1256_0->character_count) - (1)));
      for (;ROGUE_COND(((_auto_1257_0) <= (_auto_1258_0)));++_auto_1257_0)
      {
        ROGUE_GC_CHECK;
        RogueCharacter ch_0 = (RogueString_character_at(_auto_1256_0,_auto_1257_0));
        switch (ch_0)
        {
          case (RogueCharacter)'"':
          {
            RogueStringBuilder__print__String( buffer_1, Rogue_literal_strings[176] );
            break;
          }
          case (RogueCharacter)'\\':
          {
            RogueStringBuilder__print__String( buffer_1, Rogue_literal_strings[177] );
            break;
          }
          case (RogueCharacter)8:
          {
            RogueStringBuilder__print__String( buffer_1, Rogue_literal_strings[178] );
            break;
          }
          case (RogueCharacter)12:
          {
            RogueStringBuilder__print__String( buffer_1, Rogue_literal_strings[179] );
            break;
          }
          case (RogueCharacter)10:
          {
            RogueStringBuilder__print__String( buffer_1, Rogue_literal_strings[180] );
            break;
          }
          case (RogueCharacter)13:
          {
            RogueStringBuilder__print__String( buffer_1, Rogue_literal_strings[181] );
            break;
          }
          case (RogueCharacter)9:
          {
            RogueStringBuilder__print__String( buffer_1, Rogue_literal_strings[182] );
            break;
          }
          default:
          {
            if (ROGUE_COND(((((((RogueInt32)(ch_0))) >= (32))) && (((((RogueInt32)(ch_0))) <= (126))))))
            {
              RogueStringBuilder__print__Character_Logical( buffer_1, ch_0, true );
            }
            else if (ROGUE_COND(((((((((((RogueInt32)(ch_0))) < (32))) || (((((RogueInt32)(ch_0))) == (127))))) || (((((RogueInt32)(ch_0))) == (8232))))) || (((((RogueInt32)(ch_0))) == (8233))))))
            {
              RogueStringBuilder__print__String( buffer_1, Rogue_literal_strings[183] );
              RogueInt32 n_3 = (((RogueInt32)(ch_0)));
              {
                RogueInt32 nibble_5 = (0);
                RogueInt32 _auto_332_6 = (3);
                for (;ROGUE_COND(((nibble_5) <= (_auto_332_6)));++nibble_5)
                {
                  ROGUE_GC_CHECK;
                  RogueInt32 digit_4 = (((((n_3) >> (12))) & (15)));
                  n_3 = ((RogueInt32)(((n_3) << (4))));
                  if (ROGUE_COND(((digit_4) <= (9))))
                  {
                    RogueStringBuilder__print__Int32( buffer_1, digit_4 );
                  }
                  else
                  {
                    RogueStringBuilder__print__Character_Logical( buffer_1, ROGUE_ARG(((RogueCharacter)(((97) + (((digit_4) - (10))))))), true );
                  }
                }
              }
            }
            else
            {
              RogueStringBuilder__print__Character_Logical( buffer_1, ch_0, true );
            }
          }
        }
      }
    }
    RogueStringBuilder__print__Character_Logical( buffer_1, (RogueCharacter)'"', true );
  }
  else
  {
    RogueStringBuilder__print__String( buffer_1, Rogue_literal_strings[1] );
  }
  return (RogueStringBuilder*)(buffer_1);
}

void RogueStringValue__init_class()
{
  RogueStringValue_empty_string = ((RogueClassStringValue*)(RogueStringValue__init__String( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassStringValue*,ROGUE_CREATE_OBJECT(StringValue))), Rogue_literal_strings[0] )));
}

RogueOptionalInt32 RogueOptionalInt32__create()
{
  ROGUE_GC_CHECK;
  RogueInt32 default_value_0 = 0;
  return (RogueOptionalInt32)(RogueOptionalInt32( default_value_0, false ));
}

RogueOptionalByte RogueOptionalByte__create()
{
  ROGUE_GC_CHECK;
  RogueByte default_value_0 = 0;
  return (RogueOptionalByte)(RogueOptionalByte( default_value_0, false ));
}

RogueOptionalCharacter RogueOptionalCharacter__create()
{
  ROGUE_GC_CHECK;
  RogueCharacter default_value_0 = 0;
  return (RogueOptionalCharacter)(RogueOptionalCharacter( default_value_0, false ));
}


void RogueObject__init_object( RogueObject* THIS )
{
  ROGUE_GC_CHECK;
}

RogueObject* RogueObject__init( RogueObject* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueObject*)(THIS);
}

RogueInt64 RogueObject__object_id( RogueObject* THIS )
{
  ROGUE_GC_CHECK;
  RogueInt64 addr_0 = 0;
  addr_0 = (RogueInt64)(intptr_t)THIS;

  return (RogueInt64)(addr_0);
}

RogueString* RogueObject__to_String( RogueObject* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[8] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 12, ROGUE_ARG(THIS) ))) ))) )))), Rogue_literal_strings[10] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], ROGUE_ARG(((RogueString*)(RogueInt64__to_hex_string__Int32( ROGUE_ARG(((RogueObject__object_id( ROGUE_ARG(THIS) )))), 16 )))) ))) )))), Rogue_literal_strings[11] )))) ))));
}

RogueString* RogueObject__type_name( RogueObject* THIS )
{
  return (RogueString*)(Rogue_literal_strings[9]);
}

RogueClassGlobal* RogueGlobal__init_object( RogueClassGlobal* THIS )
{
  ROGUE_GC_CHECK;
  RogueObject__init_object( ROGUE_ARG(((RogueObject*)THIS)) );
  THIS->console = ((RogueClassPrintWriter*)(((RogueClassConsole*)ROGUE_SINGLETON(Console))));
  THIS->global_output_buffer = ((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )));
  return (RogueClassGlobal*)(THIS);
}

RogueClassGlobal* RogueGlobal__init( RogueClassGlobal* THIS )
{
  ROGUE_GC_CHECK;
  RogueGlobal__on_exit___Function___( ROGUE_ARG(THIS), ROGUE_ARG(((RogueClass_Function___*)(((RogueClassFunction_221*)ROGUE_SINGLETON(Function_221))))) );
  return (RogueClassGlobal*)(THIS);
}

RogueString* RogueGlobal__type_name( RogueClassGlobal* THIS )
{
  return (RogueString*)(Rogue_literal_strings[134]);
}

RogueClassGlobal* RogueGlobal__close( RogueClassGlobal* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueClassGlobal*)(THIS);
}

RogueClassGlobal* RogueGlobal__flush( RogueClassGlobal* THIS )
{
  ROGUE_GC_CHECK;
  RogueGlobal__write__StringBuilder( ROGUE_ARG(THIS), ROGUE_ARG(THIS->global_output_buffer) );
  RogueStringBuilder__clear( ROGUE_ARG(THIS->global_output_buffer) );
  return (RogueClassGlobal*)(THIS);
}

RogueClassGlobal* RogueGlobal__print__Object( RogueClassGlobal* THIS, RogueObject* value_0 )
{
  ROGUE_GC_CHECK;
  RogueStringBuilder__print__Object( ROGUE_ARG(THIS->global_output_buffer), value_0 );
  return (RogueClassGlobal*)(THIS);
}

RogueClassGlobal* RogueGlobal__print__String( RogueClassGlobal* THIS, RogueString* value_0 )
{
  ROGUE_GC_CHECK;
  RogueStringBuilder__print__String( ROGUE_ARG(THIS->global_output_buffer), value_0 );
  if (ROGUE_COND(((THIS->global_output_buffer->count) > (1024))))
  {
    RogueGlobal__flush( ROGUE_ARG(THIS) );
  }
  return (RogueClassGlobal*)(THIS);
}

RogueClassGlobal* RogueGlobal__println( RogueClassGlobal* THIS )
{
  ROGUE_GC_CHECK;
  RogueStringBuilder__print__Character_Logical( ROGUE_ARG(THIS->global_output_buffer), (RogueCharacter)10, true );
  return (RogueClassGlobal*)(((RogueClassGlobal*)(RogueGlobal__flush( ROGUE_ARG(THIS) ))));
}

RogueClassGlobal* RogueGlobal__println__Object( RogueClassGlobal* THIS, RogueObject* value_0 )
{
  ROGUE_GC_CHECK;
  return (RogueClassGlobal*)(((RogueClassGlobal*)(RogueGlobal__println( ROGUE_ARG(((RogueClassGlobal*)(RogueGlobal__print__Object( ROGUE_ARG(THIS), value_0 )))) ))));
}

RogueClassGlobal* RogueGlobal__println__String( RogueClassGlobal* THIS, RogueString* value_0 )
{
  ROGUE_GC_CHECK;
  return (RogueClassGlobal*)(((RogueClassGlobal*)(RogueGlobal__println( ROGUE_ARG(((RogueClassGlobal*)(RogueGlobal__print__String( ROGUE_ARG(THIS), value_0 )))) ))));
}

RogueClassGlobal* RogueGlobal__write__StringBuilder( RogueClassGlobal* THIS, RogueStringBuilder* buffer_0 )
{
  ROGUE_GC_CHECK;
  RoguePrintWriter__flush( ROGUE_ARG(((((RogueObject*)(RoguePrintWriter__write__StringBuilder( ROGUE_ARG(((((RogueObject*)THIS->console)))), buffer_0 )))))) );
  return (RogueClassGlobal*)(THIS);
}

RogueString* RogueGlobal__prep_arg__String( RogueClassGlobal* THIS, RogueString* arg_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(!(((((((RogueString__contains__Character( arg_0, (RogueCharacter)' ' )))) || (((RogueString__contains__Character( arg_0, (RogueCharacter)'"' )))))) || (((RogueString__contains__Character( arg_0, (RogueCharacter)'\\' ))))))))
  {
    return (RogueString*)(arg_0);
  }
  ROGUE_DEF_LOCAL_REF(RogueStringBuilder*,quoted_1,(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))));
  RogueStringBuilder__print__Character_Logical( quoted_1, (RogueCharacter)'"', true );
  {
    ROGUE_DEF_LOCAL_REF(RogueString*,_auto_222_0,(arg_0));
    RogueInt32 _auto_223_0 = (0);
    RogueInt32 _auto_224_0 = (((_auto_222_0->character_count) - (1)));
    for (;ROGUE_COND(((_auto_223_0) <= (_auto_224_0)));++_auto_223_0)
    {
      ROGUE_GC_CHECK;
      RogueCharacter ch_0 = (RogueString_character_at(_auto_222_0,_auto_223_0));
      switch (ch_0)
      {
        case (RogueCharacter)'"':
        {
          RogueStringBuilder__print__String( quoted_1, Rogue_literal_strings[176] );
          break;
        }
        case (RogueCharacter)'\\':
        {
          RogueStringBuilder__print__String( quoted_1, Rogue_literal_strings[177] );
          break;
        }
        default:
        {
          RogueStringBuilder__print__Character_Logical( quoted_1, ch_0, true );
        }
      }
    }
  }
  RogueStringBuilder__print__Character_Logical( quoted_1, (RogueCharacter)'"', true );
  return (RogueString*)(((RogueString*)(RogueStringBuilder__to_String( quoted_1 ))));
}

void RogueGlobal__require_command_line( RogueClassGlobal* THIS )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(!!(((RogueString*)(RogueSystemEnvironment__get__String( RogueSystem_environment, Rogue_literal_strings[73] ))))))
  {
    ROGUE_DEF_LOCAL_REF(RogueString*,_auto_0_0,(((RogueString*)(RogueSystemEnvironment__get__String( RogueSystem_environment, Rogue_literal_strings[74] )))));
    throw ((RogueClassError*)Rogue_call_ROGUEM1( 16, ROGUE_ARG(((RogueClassError*)(((RogueException*)Rogue_call_ROGUEM4( 13, ROGUE_ARG(((RogueException*)ROGUE_CREATE_REF(RogueClassError*,ROGUE_CREATE_OBJECT(Error)))), ROGUE_ARG(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], Rogue_literal_strings[75] ))) )))), Rogue_literal_strings[76] )))), ROGUE_ARG(((RogueString*)(RogueString__operatorPLUS__Int32( ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], Rogue_literal_strings[0] ))), 140 )))) )))), Rogue_literal_strings[77] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], ROGUE_ARG(((RogueString*)(RogueString__to_lowercase( ROGUE_ARG(((((_auto_0_0))) ? (ROGUE_ARG(_auto_0_0)) : ROGUE_ARG(Rogue_literal_strings[0]))) )))) ))) )))), Rogue_literal_strings[78] )))) )))) ))))) ));
  }
}

void RogueGlobal__save_cache( RogueClassGlobal* THIS )
{
  ROGUE_GC_CHECK;
  RogueValue__save__File_Logical_Logical( ROGUE_ARG(THIS->cache), ROGUE_ARG(((RogueClassFile*)(RogueFile__init__String( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassFile*,ROGUE_CREATE_OBJECT(File))), Rogue_literal_strings[18] )))), true, false );
}

void RogueGlobal__install_library_manager( RogueClassGlobal* THIS )
{
  ROGUE_GC_CHECK;
  {
    ROGUE_DEF_LOCAL_REF(RogueString*,_auto_232_0,((RogueSystem__os())));
    if (ROGUE_COND((RogueString__operatorEQUALSEQUALS__String_String( _auto_232_0, Rogue_literal_strings[65] ))))
    {
      RogueGlobal__install_brew( ROGUE_ARG(THIS) );
    }
    else if (ROGUE_COND((RogueString__operatorEQUALSEQUALS__String_String( _auto_232_0, Rogue_literal_strings[63] ))))
    {
    }
  }
}

void RogueGlobal__install_brew( RogueClassGlobal* THIS )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND((RogueOptionalValue__operator__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM4( 33, ROGUE_ARG(THIS->cache), Rogue_literal_strings[116] ))) ))))
  {
    return;
  }
  if (ROGUE_COND(((0) == ((RogueSystem__run__String( Rogue_literal_strings[117] ))))))
  {
    Rogue_call_ROGUEM7( 101, ROGUE_ARG(THIS->cache), Rogue_literal_strings[116], ROGUE_ARG((RogueValue__create__Logical( true ))) );
    RogueGlobal__save_cache( ROGUE_ARG(THIS) );
    return;
  }
  RogueGlobal__require_command_line( ROGUE_ARG(THIS) );
  if (ROGUE_COND(((RogueString__begins_with__Character( ROGUE_ARG(((RogueString*)(RogueString__to_lowercase( ROGUE_ARG((RogueConsole__input__String( Rogue_literal_strings[118] ))) )))), (RogueCharacter)'y' )))))
  {
    ROGUE_DEF_LOCAL_REF(RogueString*,cmd_0,(Rogue_literal_strings[119]));
    RogueGlobal__println__String( ROGUE_ARG(THIS), cmd_0 );
    if (ROGUE_COND(((0) == ((RogueSystem__run__String( cmd_0 ))))))
    {
      Rogue_call_ROGUEM7( 101, ROGUE_ARG(THIS->cache), Rogue_literal_strings[116], ROGUE_ARG((RogueValue__create__Logical( true ))) );
      RogueGlobal__save_cache( ROGUE_ARG(THIS) );
      return;
    }
    throw ((RogueClassError*)Rogue_call_ROGUEM1( 16, ROGUE_ARG(((RogueClassError*)(((RogueException*)Rogue_call_ROGUEM4( 13, ROGUE_ARG(((RogueException*)ROGUE_CREATE_REF(RogueClassError*,ROGUE_CREATE_OBJECT(Error)))), Rogue_literal_strings[120] ))))) ));
  }
  throw ((RogueClassError*)Rogue_call_ROGUEM1( 16, ROGUE_ARG(((RogueClassError*)(((RogueException*)Rogue_call_ROGUEM4( 13, ROGUE_ARG(((RogueException*)ROGUE_CREATE_REF(RogueClassError*,ROGUE_CREATE_OBJECT(Error)))), Rogue_literal_strings[121] ))))) ));
}

void RogueGlobal__install_library__Value( RogueClassGlobal* THIS, RogueClassValue* library_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND((Rogue_call_ROGUEM2( 20, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM4( 33, ROGUE_ARG(THIS->cache), Rogue_literal_strings[62] ))), ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM4( 33, library_0, Rogue_literal_strings[59] ))) ))))
  {
    return;
  }
  {
    ROGUE_DEF_LOCAL_REF(RogueString*,_auto_233_0,((RogueSystem__os())));
    if (ROGUE_COND((RogueString__operatorEQUALSEQUALS__String_String( _auto_233_0, Rogue_literal_strings[65] ))))
    {
      RogueGlobal__install_macos_library__Value( ROGUE_ARG(THIS), library_0 );
    }
    else if (ROGUE_COND((RogueString__operatorEQUALSEQUALS__String_String( _auto_233_0, Rogue_literal_strings[63] ))))
    {
      RogueGlobal__install_ubuntu_library__Value( ROGUE_ARG(THIS), library_0 );
    }
    else
    {
      throw ((RogueClassError*)Rogue_call_ROGUEM1( 16, ROGUE_ARG(((RogueClassError*)(((RogueException*)Rogue_call_ROGUEM4( 13, ROGUE_ARG(((RogueException*)ROGUE_CREATE_REF(RogueClassError*,ROGUE_CREATE_OBJECT(Error)))), ROGUE_ARG(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[106] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], ROGUE_ARG((RogueSystem__os())) ))) )))), Rogue_literal_strings[33] )))) )))) ))))) ));
    }
  }
}

void RogueGlobal__install_macos_library__Value( RogueClassGlobal* THIS, RogueClassValue* library_0 )
{
  ROGUE_GC_CHECK;
  ROGUE_DEF_LOCAL_REF(RogueString*,library_name_1,(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)((RogueClassValue*)Rogue_call_ROGUEM4( 33, library_0, Rogue_literal_strings[59] )))) ))));
  RogueLogical performed_install_2 = (false);
  if (ROGUE_COND(((0) != ((RogueSystem__run__String( ROGUE_ARG(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[71] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], library_name_1 ))) )))), Rogue_literal_strings[72] )))) )))) ))))))
  {
    RogueGlobal__require_command_line( ROGUE_ARG(THIS) );
    if (ROGUE_COND(!(((RogueString__begins_with__Character( ROGUE_ARG(((RogueString*)(RogueString__to_lowercase( ROGUE_ARG((RogueConsole__input__String( ROGUE_ARG(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[79] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], library_name_1 ))) )))), Rogue_literal_strings[80] )))) )))) ))) )))), (RogueCharacter)'y' ))))))
    {
      throw ((RogueClassError*)Rogue_call_ROGUEM1( 16, ROGUE_ARG(((RogueClassError*)(((RogueException*)Rogue_call_ROGUEM4( 13, ROGUE_ARG(((RogueException*)ROGUE_CREATE_REF(RogueClassError*,ROGUE_CREATE_OBJECT(Error)))), ROGUE_ARG(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[81] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], library_name_1 ))) )))), Rogue_literal_strings[82] )))) )))) ))))) ));
    }
    ROGUE_DEF_LOCAL_REF(RogueClassValue*,install_cmd_3,(((RogueClassValue*)Rogue_call_ROGUEM4( 33, library_0, Rogue_literal_strings[59] ))));
    if (ROGUE_COND((Rogue_call_ROGUEM2( 19, library_0, Rogue_literal_strings[83] ))))
    {
      install_cmd_3 = ((RogueClassValue*)(((RogueClassValue*)Rogue_call_ROGUEM4( 33, library_0, Rogue_literal_strings[83] ))));
    }
    ROGUE_DEF_LOCAL_REF(RogueString*,cmd_4,(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[84] )))), ROGUE_ARG(((RogueString*)(RogueString__operatorPLUS__Object( ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], Rogue_literal_strings[0] ))), ROGUE_ARG(((RogueObject*)(install_cmd_3))) )))) )))) )))));
    RogueGlobal__println__String( ROGUE_ARG(THIS), cmd_4 );
    if (ROGUE_COND(((0) != ((RogueSystem__run__String( cmd_4 ))))))
    {
      throw ((RogueClassError*)Rogue_call_ROGUEM1( 16, ROGUE_ARG(((RogueClassError*)(((RogueException*)Rogue_call_ROGUEM4( 13, ROGUE_ARG(((RogueException*)ROGUE_CREATE_REF(RogueClassError*,ROGUE_CREATE_OBJECT(Error)))), ROGUE_ARG(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[85] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], library_name_1 ))) )))), Rogue_literal_strings[82] )))) )))) ))))) ));
    }
    performed_install_2 = ((RogueLogical)(true));
  }
  if (ROGUE_COND(!(performed_install_2)))
  {
    RogueGlobal__println__String( ROGUE_ARG(THIS), ROGUE_ARG(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[86] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], library_name_1 ))) )))), Rogue_literal_strings[87] )))) )))) );
  }
  if (ROGUE_COND(((0) != ((RogueSystem__run__String( ROGUE_ARG(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[71] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], library_name_1 ))) )))), Rogue_literal_strings[88] )))) )))) ))))))
  {
    throw ((RogueClassError*)Rogue_call_ROGUEM1( 16, ROGUE_ARG(((RogueClassError*)(((RogueException*)Rogue_call_ROGUEM4( 13, ROGUE_ARG(((RogueException*)ROGUE_CREATE_REF(RogueClassError*,ROGUE_CREATE_OBJECT(Error)))), ROGUE_ARG(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[89] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], library_name_1 ))) )))), Rogue_literal_strings[82] )))) )))) ))))) ));
  }
  ROGUE_DEF_LOCAL_REF(RogueString*,header_path_5,0);
  ROGUE_DEF_LOCAL_REF(RogueString*,library_path_6,0);
  if (ROGUE_COND((Rogue_call_ROGUEM2( 19, library_0, Rogue_literal_strings[90] ))))
  {
    header_path_5 = ((RogueString*)(((RogueString*)(RogueGlobal__find_path__Value_String( ROGUE_ARG(THIS), library_0, ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)((RogueClassValue*)Rogue_call_ROGUEM4( 33, library_0, Rogue_literal_strings[90] )))) ))) )))));
  }
  if (ROGUE_COND((Rogue_call_ROGUEM2( 19, library_0, Rogue_literal_strings[93] ))))
  {
    library_path_6 = ((RogueString*)(((RogueString*)(RogueGlobal__find_path__Value_String( ROGUE_ARG(THIS), library_0, ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)((RogueClassValue*)Rogue_call_ROGUEM4( 33, library_0, Rogue_literal_strings[93] )))) ))) )))));
  }
  if (ROGUE_COND(!(!!(header_path_5))))
  {
    header_path_5 = ((RogueString*)(((RogueString*)(RogueGlobal__find_path__Value_String( ROGUE_ARG(THIS), library_0, Rogue_literal_strings[94] )))));
  }
  if (ROGUE_COND(!!(header_path_5)))
  {
    RogueGlobal__println__String( ROGUE_ARG(THIS), ROGUE_ARG(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[95] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], header_path_5 ))) )))) )))) );
  }
  if (ROGUE_COND(!(!!(library_path_6))))
  {
    library_path_6 = ((RogueString*)(((RogueString*)(RogueGlobal__find_path__Value_String( ROGUE_ARG(THIS), library_0, Rogue_literal_strings[96] )))));
  }
  if (ROGUE_COND(!(!!(library_path_6))))
  {
    library_path_6 = ((RogueString*)(((RogueString*)(RogueGlobal__find_path__Value_String( ROGUE_ARG(THIS), library_0, Rogue_literal_strings[97] )))));
  }
  if (ROGUE_COND(!(!!(library_path_6))))
  {
    library_path_6 = ((RogueString*)(((RogueString*)(RogueGlobal__find_path__Value_String( ROGUE_ARG(THIS), library_0, Rogue_literal_strings[98] )))));
  }
  if (ROGUE_COND(!!(library_path_6)))
  {
    RogueGlobal__println__String( ROGUE_ARG(THIS), ROGUE_ARG(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[99] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], library_path_6 ))) )))) )))) );
    RogueGlobal__println( ROGUE_ARG(THIS) );
  }
  RogueFile__delete__String( Rogue_literal_strings[92] );
  if (ROGUE_COND(((!!(library_path_6)) && (!!(header_path_5)))))
  {
    RogueValueTable__set__String_Value( ROGUE_ARG(((RogueClassValueTable*)Rogue_call_ROGUEM4( 29, ROGUE_ARG(THIS->cache), Rogue_literal_strings[62] ))), library_name_1, ROGUE_ARG((RogueValue__create__Logical( true ))) );
    RogueValueTable__set__String_Value( ROGUE_ARG(((RogueClassValueTable*)Rogue_call_ROGUEM4( 29, ROGUE_ARG(THIS->cache), Rogue_literal_strings[100] ))), library_name_1, ROGUE_ARG((RogueValue__create__String( header_path_5 ))) );
    RogueValueTable__set__String_Value( ROGUE_ARG(((RogueClassValueTable*)Rogue_call_ROGUEM4( 29, ROGUE_ARG(THIS->cache), Rogue_literal_strings[101] ))), library_name_1, ROGUE_ARG((RogueValue__create__String( library_path_6 ))) );
    RogueGlobal__save_cache( ROGUE_ARG(THIS) );
    return;
  }
  throw ((RogueClassError*)Rogue_call_ROGUEM1( 16, ROGUE_ARG(((RogueClassError*)(((RogueException*)Rogue_call_ROGUEM4( 13, ROGUE_ARG(((RogueException*)ROGUE_CREATE_REF(RogueClassError*,ROGUE_CREATE_OBJECT(Error)))), ROGUE_ARG(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[102] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], library_name_1 ))) )))), Rogue_literal_strings[82] )))) )))) ))))) ));
}

RogueString* RogueGlobal__find_path__Value_String( RogueClassGlobal* THIS, RogueClassValue* library_0, RogueString* pattern_1 )
{
  ROGUE_GC_CHECK;
  ROGUE_DEF_LOCAL_REF(RogueString*,original_pattern_2,(pattern_1));
  if (ROGUE_COND(!(((RogueString__begins_with__Character( pattern_1, (RogueCharacter)'/' ))))))
  {
    pattern_1 = ((RogueString*)((RogueString__operatorPLUS__String_String( Rogue_literal_strings[91], pattern_1 ))));
  }
  ROGUE_DEF_LOCAL_REF(RogueClassLineReader*,reader_3,(((RogueClassLineReader*)(RogueLineReader__init__File( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassLineReader*,ROGUE_CREATE_OBJECT(LineReader))), ROGUE_ARG(((RogueClassFile*)(RogueFile__init__String( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassFile*,ROGUE_CREATE_OBJECT(File))), Rogue_literal_strings[92] )))) )))));
  {
    ROGUE_DEF_LOCAL_REF(RogueClassLineReader*,_auto_240_0,(reader_3));
    while (ROGUE_COND(((RogueLineReader__has_another( _auto_240_0 )))))
    {
      ROGUE_GC_CHECK;
      ROGUE_DEF_LOCAL_REF(RogueString*,line_0,(((RogueString*)(RogueLineReader__read( _auto_240_0 )))));
      line_0 = ((RogueString*)(((RogueString*)(RogueString__trimmed( line_0 )))));
      if (ROGUE_COND((RogueFile__matches_wildcard_pattern__String_String( line_0, pattern_1 ))))
      {
        RogueInt32 path_len_4 = (0);
        while (ROGUE_COND(!((RogueFile___matches_wildcard_pattern__String_Int32_Int32_String_Int32_Int32( line_0, path_len_4, ROGUE_ARG(((line_0->character_count) - (path_len_4))), original_pattern_2, 0, ROGUE_ARG(original_pattern_2->character_count) )))))
        {
          ROGUE_GC_CHECK;
          ++path_len_4;
        }
        RogueLineReader__close( reader_3 );
        return (RogueString*)(((RogueString*)(RogueString__leftmost__Int32( line_0, path_len_4 ))));
      }
    }
  }
  RogueLineReader__close( reader_3 );
  return (RogueString*)(((RogueString*)(NULL)));
}

void RogueGlobal__install_ubuntu_library__Value( RogueClassGlobal* THIS, RogueClassValue* library_0 )
{
  ROGUE_GC_CHECK;
  ROGUE_DEF_LOCAL_REF(RogueString*,library_name_1,(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)((RogueClassValue*)Rogue_call_ROGUEM4( 33, library_0, Rogue_literal_strings[59] )))) ))));
  ROGUE_DEF_LOCAL_REF(RogueString*,cmd_2,(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[103] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], library_name_1 ))) )))), Rogue_literal_strings[72] )))) )))));
  RogueGlobal__println__String( ROGUE_ARG(THIS), ROGUE_ARG(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[104] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], library_name_1 ))) )))), Rogue_literal_strings[54] )))) )))) );
  RogueGlobal__flush( ROGUE_ARG(((RogueClassGlobal*)(RogueGlobal__println__String( ROGUE_ARG(THIS), cmd_2 )))) );
  if (ROGUE_COND(((0) != ((RogueSystem__run__String( cmd_2 ))))))
  {
    RogueGlobal__require_command_line( ROGUE_ARG(THIS) );
    if (ROGUE_COND(!(((RogueString__begins_with__Character( ROGUE_ARG(((RogueString*)(RogueString__to_lowercase( ROGUE_ARG((RogueConsole__input__String( ROGUE_ARG(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[79] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], library_name_1 ))) )))), Rogue_literal_strings[80] )))) )))) ))) )))), (RogueCharacter)'y' ))))))
    {
      throw ((RogueClassError*)Rogue_call_ROGUEM1( 16, ROGUE_ARG(((RogueClassError*)(((RogueException*)Rogue_call_ROGUEM4( 13, ROGUE_ARG(((RogueException*)ROGUE_CREATE_REF(RogueClassError*,ROGUE_CREATE_OBJECT(Error)))), ROGUE_ARG(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[81] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], library_name_1 ))) )))), Rogue_literal_strings[82] )))) )))) ))))) ));
    }
    ROGUE_DEF_LOCAL_REF(RogueClassValue*,install_cmd_3,(((RogueClassValue*)Rogue_call_ROGUEM4( 33, library_0, Rogue_literal_strings[59] ))));
    if (ROGUE_COND((Rogue_call_ROGUEM2( 19, library_0, Rogue_literal_strings[83] ))))
    {
      install_cmd_3 = ((RogueClassValue*)(((RogueClassValue*)Rogue_call_ROGUEM4( 33, library_0, Rogue_literal_strings[83] ))));
    }
    cmd_2 = ((RogueString*)(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[105] )))), ROGUE_ARG(((RogueString*)(RogueString__operatorPLUS__Object( ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], Rogue_literal_strings[0] ))), ROGUE_ARG(((RogueObject*)(install_cmd_3))) )))) )))) )))));
    RogueGlobal__println__String( ROGUE_ARG(THIS), cmd_2 );
    if (ROGUE_COND(((0) != ((RogueSystem__run__String( cmd_2 ))))))
    {
      throw ((RogueClassError*)Rogue_call_ROGUEM1( 16, ROGUE_ARG(((RogueClassError*)(((RogueException*)Rogue_call_ROGUEM4( 13, ROGUE_ARG(((RogueException*)ROGUE_CREATE_REF(RogueClassError*,ROGUE_CREATE_OBJECT(Error)))), ROGUE_ARG(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[85] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], library_name_1 ))) )))), Rogue_literal_strings[82] )))) )))) ))))) ));
    }
  }
  RogueValueTable__set__String_Value( ROGUE_ARG(((RogueClassValueTable*)Rogue_call_ROGUEM4( 29, ROGUE_ARG(THIS->cache), Rogue_literal_strings[62] ))), library_name_1, ROGUE_ARG((RogueValue__create__Logical( true ))) );
  RogueGlobal__save_cache( ROGUE_ARG(THIS) );
}

RogueString* RogueGlobal__library_location__Value( RogueClassGlobal* THIS, RogueClassValue* library_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(!((Rogue_call_ROGUEM2( 20, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM4( 33, ROGUE_ARG(THIS->cache), Rogue_literal_strings[62] ))), ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM4( 33, library_0, Rogue_literal_strings[59] ))) )))))
  {
    RogueGlobal__install_library__Value( ROGUE_ARG(THIS), library_0 );
  }
  ROGUE_DEF_LOCAL_REF(RogueClassValue*,path_1,(((RogueClassValue*)Rogue_call_ROGUEM4( 33, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM4( 33, ROGUE_ARG(THIS->cache), Rogue_literal_strings[101] ))), ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)((RogueClassValue*)Rogue_call_ROGUEM4( 33, library_0, Rogue_literal_strings[59] )))) ))) ))));
  if (ROGUE_COND((RogueFile__exists__String( ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ((RogueObject*)path_1) ))) ))))
  {
    return (RogueString*)(((RogueString*)Rogue_call_ROGUEM1( 6, ((RogueObject*)path_1) )));
  }
  RogueGlobal__println__String( ROGUE_ARG(THIS), ROGUE_ARG(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[86] )))), ROGUE_ARG(((RogueString*)(RogueString__operatorPLUS__Object( ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], Rogue_literal_strings[0] ))), ROGUE_ARG(((RogueObject*)(((RogueClassValue*)Rogue_call_ROGUEM4( 33, library_0, Rogue_literal_strings[59] ))))) )))) )))), Rogue_literal_strings[87] )))) )))) );
  Rogue_call_ROGUEM4( 91, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM4( 33, ROGUE_ARG(THIS->cache), Rogue_literal_strings[62] ))), ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM4( 33, library_0, Rogue_literal_strings[59] ))) );
  RogueGlobal__save_cache( ROGUE_ARG(THIS) );
  return (RogueString*)(((RogueString*)(RogueGlobal__library_location__Value( ROGUE_ARG(THIS), library_0 ))));
}

RogueString* RogueGlobal__header_location__Value( RogueClassGlobal* THIS, RogueClassValue* library_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(!((Rogue_call_ROGUEM2( 20, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM4( 33, ROGUE_ARG(THIS->cache), Rogue_literal_strings[62] ))), ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM4( 33, library_0, Rogue_literal_strings[59] ))) )))))
  {
    RogueGlobal__install_library__Value( ROGUE_ARG(THIS), library_0 );
  }
  ROGUE_DEF_LOCAL_REF(RogueClassValue*,path_1,(((RogueClassValue*)Rogue_call_ROGUEM4( 33, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM4( 33, ROGUE_ARG(THIS->cache), Rogue_literal_strings[100] ))), ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)((RogueClassValue*)Rogue_call_ROGUEM4( 33, library_0, Rogue_literal_strings[59] )))) ))) ))));
  if (ROGUE_COND((RogueFile__exists__String( ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ((RogueObject*)path_1) ))) ))))
  {
    return (RogueString*)(((RogueString*)Rogue_call_ROGUEM1( 6, ((RogueObject*)path_1) )));
  }
  RogueGlobal__println__String( ROGUE_ARG(THIS), ROGUE_ARG(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[86] )))), ROGUE_ARG(((RogueString*)(RogueString__operatorPLUS__Object( ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], Rogue_literal_strings[0] ))), ROGUE_ARG(((RogueObject*)(((RogueClassValue*)Rogue_call_ROGUEM4( 33, library_0, Rogue_literal_strings[59] ))))) )))) )))), Rogue_literal_strings[87] )))) )))) );
  Rogue_call_ROGUEM4( 91, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM4( 33, ROGUE_ARG(THIS->cache), Rogue_literal_strings[62] ))), ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM4( 33, library_0, Rogue_literal_strings[59] ))) );
  RogueGlobal__save_cache( ROGUE_ARG(THIS) );
  return (RogueString*)(((RogueString*)(RogueGlobal__header_location__Value( ROGUE_ARG(THIS), library_0 ))));
}

void RogueGlobal__scan_config__File( RogueClassGlobal* THIS, RogueClassFile* file_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(!((RogueFile__exists__String( ROGUE_ARG(file_0->filepath) )))))
  {
    return;
  }
  ROGUE_DEF_LOCAL_REF(RogueClassPrintWriter*,writer_1,((RoguePrintWriter__create__Writer_Byte_( ROGUE_ARG(((((RogueClassWriter_Byte_*)((RogueFile__writer__String( ROGUE_ARG(((RogueClassFile*)(RogueFile__init__String( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassFile*,ROGUE_CREATE_OBJECT(File))), ROGUE_ARG(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[52] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], ROGUE_ARG(((RogueString*)(RogueFile__filename( file_0 )))) ))) )))) )))) )))->filepath) ))))))) ))));
  {
    ROGUE_DEF_LOCAL_REF(RogueClassLineReader*,_auto_244_0,(((RogueClassLineReader*)(RogueLineReader__init__File( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassLineReader*,ROGUE_CREATE_OBJECT(LineReader))), file_0 )))));
    while (ROGUE_COND(((RogueLineReader__has_another( _auto_244_0 )))))
    {
      ROGUE_GC_CHECK;
      ROGUE_DEF_LOCAL_REF(RogueString*,original_line_0,(((RogueString*)(RogueLineReader__read( _auto_244_0 )))));
      ROGUE_DEF_LOCAL_REF(RogueString*,line_2,(((RogueString*)(RogueString__trimmed( original_line_0 )))));
      if (ROGUE_COND(((RogueString__begins_with__String( line_2, Rogue_literal_strings[55] )))))
      {
        ROGUE_DEF_LOCAL_REF(RogueString*,cmd_3,(((RogueString*)(RogueString__rightmost__Int32( line_2, -2 )))));
        ROGUE_DEF_LOCAL_REF(RogueString*,args_4,(((RogueString*)(RogueString__trimmed( ROGUE_ARG(((RogueString*)(RogueString__after_first__String( cmd_3, Rogue_literal_strings[5] )))) )))));
        cmd_3 = ((RogueString*)(((RogueString*)(RogueString__to_lowercase( ROGUE_ARG(((RogueString*)(RogueString__trimmed( ROGUE_ARG(((RogueString*)(RogueString__before_first__String( cmd_3, Rogue_literal_strings[5] )))) )))) )))));
        {
          ROGUE_DEF_LOCAL_REF(RogueString*,_auto_261_0,(((RogueString*)(RogueString__before_first__Character( cmd_3, (RogueCharacter)'(' )))));
          if (ROGUE_COND((((RogueString__operatorEQUALSEQUALS__String_String( _auto_261_0, Rogue_literal_strings[56] ))) || ((RogueString__operatorEQUALSEQUALS__String_String( _auto_261_0, Rogue_literal_strings[16] ))))))
          {
            if (ROGUE_COND(((RogueString__begins_with__Character( args_4, (RogueCharacter)'"' )))))
            {
              args_4 = ((RogueString*)(((RogueString*)(RogueString__before_last__Character( ROGUE_ARG(((RogueString*)(RogueString__after_first__Character( args_4, (RogueCharacter)'"' )))), (RogueCharacter)'"' )))));
            }
            RogueValueTable__set__String_Value( ROGUE_ARG(THIS->config), cmd_3, ROGUE_ARG((RogueValue__create__String( args_4 ))) );
          }
          else if (ROGUE_COND((RogueString__operatorEQUALSEQUALS__String_String( _auto_261_0, Rogue_literal_strings[57] ))))
          {
            if (ROGUE_COND(!!(args_4->character_count)))
            {
              ROGUE_DEF_LOCAL_REF(RogueClassJSONParser*,parser_5,(((RogueClassJSONParser*)(RogueJSONParser__init__Scanner( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassJSONParser*,ROGUE_CREATE_OBJECT(JSONParser))), ROGUE_ARG(((RogueClassScanner*)(RogueScanner__init__String_Int32_Logical( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassScanner*,ROGUE_CREATE_OBJECT(Scanner))), args_4, 0, false )))) )))));
              RogueJSONParser__consume_spaces( parser_5 );
              while (ROGUE_COND(((RogueJSONParser__has_another( parser_5 )))))
              {
                ROGUE_GC_CHECK;
                ROGUE_DEF_LOCAL_REF(RogueString*,name_6,(((RogueString*)(RogueGlobal__parse_filepath__JSONParser( ROGUE_ARG(THIS), parser_5 )))));
                ROGUE_DEF_LOCAL_REF(RogueClassValueTable*,_auto_258_0,(((RogueClassValueTable*)(RogueValueTable__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassValueTable*,ROGUE_CREATE_OBJECT(ValueTable))) )))));
                {
                  RogueValueTable__set__String_Value( _auto_258_0, Rogue_literal_strings[59], ROGUE_ARG((RogueValue__create__String( name_6 ))) );
                }
                ROGUE_DEF_LOCAL_REF(RogueClassValueTable*,entry_7,(_auto_258_0));
                RogueValueList__add__Value( ROGUE_ARG(((RogueClassValueList*)(RogueValueTable__ensure_list__String( ROGUE_ARG(THIS->config), cmd_3 )))), ROGUE_ARG(((RogueClassValue*)(entry_7))) );
                RogueJSONParser__consume_spaces( parser_5 );
                if (ROGUE_COND(((RogueJSONParser__consume__Character( parser_5, (RogueCharacter)'(' )))))
                {
                  RogueJSONParser__consume_spaces( parser_5 );
                  while (ROGUE_COND(((RogueJSONParser__has_another( parser_5 )))))
                  {
                    ROGUE_GC_CHECK;
                    if (ROGUE_COND(((RogueJSONParser__consume__Character( parser_5, (RogueCharacter)')' )))))
                    {
                      goto _auto_260;
                    }
                    ROGUE_DEF_LOCAL_REF(RogueString*,key_8,(((RogueString*)(RogueJSONParser__parse_identifier( parser_5 )))));
                    RogueJSONParser__consume_spaces( parser_5 );
                    if (ROGUE_COND(((RogueJSONParser__consume__Character( parser_5, (RogueCharacter)':' )))))
                    {
                      RogueJSONParser__consume_spaces( parser_5 );
                      ROGUE_DEF_LOCAL_REF(RogueString*,value_9,(((RogueString*)(RogueGlobal__parse_filepath__JSONParser( ROGUE_ARG(THIS), parser_5 )))));
                      RogueValueTable__set__String_Value( entry_7, key_8, ROGUE_ARG((RogueValue__create__String( value_9 ))) );
                    }
                    RogueJSONParser__consume_spaces( parser_5 );
                  }
                  _auto_260:;
                }
                RogueJSONParser__consume_spaces( parser_5 );
              }
            }
          }
          else if (ROGUE_COND(((RogueValueTable__contains__String( ROGUE_ARG(THIS->config), cmd_3 )))))
          {
            RogueValueTable__set__String_Value( ROGUE_ARG(THIS->config), cmd_3, ROGUE_ARG((RogueValue__create__String( ROGUE_ARG(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), ROGUE_ARG(((RogueString*)(RogueString__operatorPLUS__Object( ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], Rogue_literal_strings[0] ))), ROGUE_ARG(((RogueObject*)(((RogueClassValue*)(RogueValueTable__get__String( ROGUE_ARG(THIS->config), cmd_3 )))))) )))) )))), Rogue_literal_strings[60] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], args_4 ))) )))) )))) ))) );
          }
          else
          {
            RogueValueTable__set__String_Value( ROGUE_ARG(THIS->config), cmd_3, ROGUE_ARG((RogueValue__create__String( args_4 ))) );
          }
        }
      }
      line_2 = ((RogueString*)(original_line_0));
      RogueOptionalInt32 dollar_10 = (((RogueString__locate__Character_OptionalInt32( line_2, (RogueCharacter)'$', (RogueOptionalInt32__create()) ))));
      while (ROGUE_COND(dollar_10.exists))
      {
        ROGUE_GC_CHECK;
        if (ROGUE_COND(((RogueString__contains_at__String_Int32( line_2, Rogue_literal_strings[61], ROGUE_ARG(dollar_10.value) )))))
        {
          RogueOptionalInt32 close_paren_11 = (((RogueString__locate__Character_OptionalInt32( line_2, (RogueCharacter)')', RogueOptionalInt32( ((dollar_10.value) + (1)), true ) ))));
          if (ROGUE_COND(close_paren_11.exists))
          {
            ROGUE_DEF_LOCAL_REF(RogueString*,library_name_12,(((RogueString*)(RogueString__trimmed( ROGUE_ARG(((RogueString*)(RogueString__from__Int32_Int32( line_2, ROGUE_ARG(((dollar_10.value) + (8))), ROGUE_ARG(((close_paren_11.value) - (1))) )))) )))));
            ROGUE_DEF_LOCAL_REF(RogueClassValue*,library_13,0);
            {
              {
                ROGUE_DEF_LOCAL_REF(RogueClassValue*,_auto_262_0,(((RogueClassValue*)(RogueValueTable__get__String( ROGUE_ARG(THIS->config), Rogue_literal_strings[57] )))));
                RogueInt32 _auto_263_0 = (0);
                RogueInt32 _auto_264_0 = ((((Rogue_call_ROGUEM3( 22, _auto_262_0 ))) - (1)));
                for (;ROGUE_COND(((_auto_263_0) <= (_auto_264_0)));++_auto_263_0)
                {
                  ROGUE_GC_CHECK;
                  ROGUE_DEF_LOCAL_REF(RogueClassValue*,lib_0,(((RogueClassValue*)Rogue_call_ROGUEM5( 32, _auto_262_0, _auto_263_0 ))));
                  if (ROGUE_COND(((RogueValue__operatorEQUALSEQUALS__String( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM4( 33, lib_0, Rogue_literal_strings[59] ))), library_name_12 )))))
                  {
                    library_13 = ((RogueClassValue*)(lib_0));
                    goto _auto_265;
                  }
                }
              }
              ROGUE_DEF_LOCAL_REF(RogueClassValueTable*,_auto_266_0,(((RogueClassValueTable*)(RogueValueTable__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassValueTable*,ROGUE_CREATE_OBJECT(ValueTable))) )))));
              {
                RogueValueTable__set__String_Value( _auto_266_0, Rogue_literal_strings[59], ROGUE_ARG((RogueValue__create__String( library_name_12 ))) );
              }
              library_13 = ((RogueClassValue*)(((RogueClassValue*)(_auto_266_0))));
            }
            _auto_265:;
            line_2 = ((RogueString*)(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], ROGUE_ARG(((RogueString*)(RogueString__before__Int32( line_2, ROGUE_ARG(dollar_10.value) )))) ))) )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], ROGUE_ARG(((RogueString*)(RogueGlobal__header_location__Value( ROGUE_ARG(THIS), library_13 )))) ))) )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], ROGUE_ARG(((RogueString*)(RogueString__after__Int32( line_2, ROGUE_ARG(close_paren_11.value) )))) ))) )))) )))));
          }
        }
        else if (ROGUE_COND(((RogueString__contains_at__String_Int32( line_2, Rogue_literal_strings[107], ROGUE_ARG(dollar_10.value) )))))
        {
          RogueOptionalInt32 close_paren_14 = (((RogueString__locate__Character_OptionalInt32( line_2, (RogueCharacter)')', RogueOptionalInt32( ((dollar_10.value) + (1)), true ) ))));
          if (ROGUE_COND(close_paren_14.exists))
          {
            ROGUE_DEF_LOCAL_REF(RogueString*,library_name_15,(((RogueString*)(RogueString__trimmed( ROGUE_ARG(((RogueString*)(RogueString__from__Int32_Int32( line_2, ROGUE_ARG(((dollar_10.value) + (9))), ROGUE_ARG(((close_paren_14.value) - (1))) )))) )))));
            ROGUE_DEF_LOCAL_REF(RogueClassValue*,library_16,0);
            {
              {
                ROGUE_DEF_LOCAL_REF(RogueClassValue*,_auto_267_0,(((RogueClassValue*)(RogueValueTable__get__String( ROGUE_ARG(THIS->config), Rogue_literal_strings[57] )))));
                RogueInt32 _auto_268_0 = (0);
                RogueInt32 _auto_269_0 = ((((Rogue_call_ROGUEM3( 22, _auto_267_0 ))) - (1)));
                for (;ROGUE_COND(((_auto_268_0) <= (_auto_269_0)));++_auto_268_0)
                {
                  ROGUE_GC_CHECK;
                  ROGUE_DEF_LOCAL_REF(RogueClassValue*,lib_0,(((RogueClassValue*)Rogue_call_ROGUEM5( 32, _auto_267_0, _auto_268_0 ))));
                  if (ROGUE_COND(((RogueValue__operatorEQUALSEQUALS__String( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM4( 33, lib_0, Rogue_literal_strings[59] ))), library_name_15 )))))
                  {
                    library_16 = ((RogueClassValue*)(lib_0));
                    goto _auto_270;
                  }
                }
              }
              ROGUE_DEF_LOCAL_REF(RogueClassValueTable*,_auto_271_0,(((RogueClassValueTable*)(RogueValueTable__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassValueTable*,ROGUE_CREATE_OBJECT(ValueTable))) )))));
              {
                RogueValueTable__set__String_Value( _auto_271_0, Rogue_literal_strings[59], ROGUE_ARG((RogueValue__create__String( library_name_15 ))) );
              }
              library_16 = ((RogueClassValue*)(((RogueClassValue*)(_auto_271_0))));
            }
            _auto_270:;
            line_2 = ((RogueString*)(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], ROGUE_ARG(((RogueString*)(RogueString__before__Int32( line_2, ROGUE_ARG(dollar_10.value) )))) ))) )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], ROGUE_ARG(((RogueString*)(RogueGlobal__library_location__Value( ROGUE_ARG(THIS), library_16 )))) ))) )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], ROGUE_ARG(((RogueString*)(RogueString__after__Int32( line_2, ROGUE_ARG(close_paren_14.value) )))) ))) )))) )))));
          }
        }
        dollar_10 = ((RogueOptionalInt32)(((RogueString__locate__Character_OptionalInt32( line_2, (RogueCharacter)'$', RogueOptionalInt32( ((dollar_10.value) + (1)), true ) )))));
      }
      RoguePrintWriter__println__String( ((((RogueObject*)writer_1))), line_2 );
    }
  }
  RoguePrintWriter__close( ((((RogueObject*)writer_1))) );
}

RogueString* RogueGlobal__parse_filepath__JSONParser( RogueClassGlobal* THIS, RogueClassJSONParser* parser_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((RogueJSONParser__next_is__Character( parser_0, (RogueCharacter)'"' )))))
  {
    return (RogueString*)(((RogueString*)(RogueJSONParser__parse_string( parser_0 ))));
  }
  ROGUE_DEF_LOCAL_REF(RogueStringBuilder*,buffer_1,(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))));
  while (ROGUE_COND(((((RogueJSONParser__has_another( parser_0 )))) && (!(((RogueString__contains__Character( Rogue_literal_strings[58], ROGUE_ARG(((RogueJSONParser__peek( parser_0 )))) ))))))))
  {
    ROGUE_GC_CHECK;
    RogueStringBuilder__print__Character_Logical( buffer_1, ROGUE_ARG(((RogueJSONParser__read( parser_0 )))), true );
  }
  if (ROGUE_COND(((buffer_1->count) == (0))))
  {
    RogueStringBuilder__print__Character_Logical( buffer_1, ROGUE_ARG(((RogueJSONParser__read( parser_0 )))), true );
  }
  return (RogueString*)(((RogueString*)(RogueStringBuilder__to_String( buffer_1 ))));
}

void RogueGlobal__on_launch( RogueClassGlobal* THIS )
{
  ROGUE_GC_CHECK;
  RogueFile__create_folder__String( Rogue_literal_strings[15] );
  ROGUE_DEF_LOCAL_REF(RogueClassValueTable*,_auto_272_0,(((RogueClassValueTable*)(RogueValueTable__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassValueTable*,ROGUE_CREATE_OBJECT(ValueTable))) )))));
  {
    RogueValueTable__set__String_Value( _auto_272_0, Rogue_literal_strings[16], ROGUE_ARG((RogueValue__create__String( Rogue_literal_strings[16] ))) );
    RogueValueTable__set__String_Value( _auto_272_0, Rogue_literal_strings[17], ROGUE_ARG((RogueValue__create__String( Rogue_literal_strings[18] ))) );
  }
  ((RogueClassGlobal*)ROGUE_SINGLETON(Global))->config = _auto_272_0;
  ((RogueClassGlobal*)ROGUE_SINGLETON(Global))->cache = (RogueJSON__load_table__File( ROGUE_ARG(((RogueClassFile*)(RogueFile__init__String( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassFile*,ROGUE_CREATE_OBJECT(File))), Rogue_literal_strings[18] )))) ));
  ROGUE_DEF_LOCAL_REF(RogueString*,compiler_invocation_0,0);
#if defined(DEFAULT_CXX)
    compiler_invocation_0 = RogueString_create_from_utf8( DEFAULT_CXX );
#else
    compiler_invocation_0 = RogueString_create_from_utf8( "g++ -Wall -std=gnu++11 -fno-strict-aliasing -Wno-invalid-offsetof" );
#endif

  RogueValueTable__set__String_Value( ROGUE_ARG(THIS->config), Rogue_literal_strings[26], ROGUE_ARG((RogueValue__create__String( compiler_invocation_0 ))) );
  RogueLogical has_build_core_1 = ((RogueFile__exists__String( Rogue_literal_strings[27] )));
  ROGUE_DEF_LOCAL_REF(RogueString*,buildfile_2,0);
  {
    ROGUE_DEF_LOCAL_REF(RogueString_List*,_auto_273_0,(RogueSystem_command_line_arguments));
    RogueInt32 _auto_274_0 = (0);
    RogueInt32 _auto_275_0 = (((_auto_273_0->count) - (1)));
    for (;ROGUE_COND(((_auto_274_0) <= (_auto_275_0)));++_auto_274_0)
    {
      ROGUE_GC_CHECK;
      ROGUE_DEF_LOCAL_REF(RogueString*,arg_0,(((RogueString*)(_auto_273_0->data->as_objects[_auto_274_0]))));
      if (ROGUE_COND((((RogueString__operatorEQUALSEQUALS__String_String( arg_0, Rogue_literal_strings[28] ))) || (((RogueString__begins_with__String( arg_0, Rogue_literal_strings[29] )))))))
      {
        buildfile_2 = ((RogueString*)(((RogueString*)(RogueString__after_first__Character( arg_0, (RogueCharacter)'=' )))));
        if (ROGUE_COND(((buildfile_2->character_count) == (0))))
        {
          RogueConsoleErrorPrinter__println__String( ROGUE_ARG(((RogueClassConsole*)ROGUE_SINGLETON(Console))->error), Rogue_literal_strings[30] );
          RogueSystem__exit__Int32( 1 );
        }
        if (ROGUE_COND(!((RogueFile__exists__String( buildfile_2 )))))
        {
          if (ROGUE_COND((RogueFile__exists__String( ROGUE_ARG((RogueString__operatorPLUS__String_String( buildfile_2, Rogue_literal_strings[31] ))) ))))
          {
            buildfile_2 = ((RogueString*)(((RogueString*)(RogueString__operatorPLUS__String( buildfile_2, Rogue_literal_strings[31] )))));
          }
          else
          {
            RogueConsoleErrorPrinter__println__String( ROGUE_ARG(((RogueClassConsole*)ROGUE_SINGLETON(Console))->error), ROGUE_ARG(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[32] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], buildfile_2 ))) )))), Rogue_literal_strings[33] )))) )))) );
            RogueSystem__exit__Int32( 1 );
          }
        }
        RogueString_List__remove__String( ROGUE_ARG(RogueSystem_command_line_arguments), arg_0 );
        goto _auto_276;
      }
    }
  }
  _auto_276:;
  if (ROGUE_COND(((((void*)buildfile_2) == ((void*)NULL)) && (!((RogueFile__exists__String( Rogue_literal_strings[46] )))))))
  {
    if (ROGUE_COND(has_build_core_1))
    {
      RogueGlobal__println__String( ROGUE_ARG(THIS), Rogue_literal_strings[47] );
      RogueFile__save__String_String( ROGUE_ARG(((RogueClassFile*)(RogueFile__init__String( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassFile*,ROGUE_CREATE_OBJECT(File))), Rogue_literal_strings[46] )))->filepath), Rogue_literal_strings[49] );
    }
    else
    {
      RogueConsoleErrorPrinter__println__String( ROGUE_ARG(((RogueClassConsole*)ROGUE_SINGLETON(Console))->error), Rogue_literal_strings[50] );
    }
    RogueSystem__exit__Int32( 1 );
  }
  RogueLogical using_buildcore_3 = 0;
  ROGUE_DEF_LOCAL_REF(RogueString*,exe_4,0);
  if (ROGUE_COND(((void*)buildfile_2) == ((void*)NULL)))
  {
    buildfile_2 = ((RogueString*)(Rogue_literal_strings[46]));
    exe_4 = ((RogueString*)(Rogue_literal_strings[51]));
    using_buildcore_3 = ((RogueLogical)(has_build_core_1));
  }
  else
  {
    exe_4 = ((RogueString*)((RogueString__operatorPLUS__String_String( Rogue_literal_strings[52], ROGUE_ARG(((RogueString*)(RogueString__before_first__Character( buildfile_2, (RogueCharacter)'.' )))) ))));
  }
  if (ROGUE_COND((((RogueFile__is_newer_than__String_String( buildfile_2, exe_4 ))) || (((using_buildcore_3) && ((RogueFile__is_newer_than__String_String( Rogue_literal_strings[27], exe_4 ))))))))
  {
    RogueGlobal__println__String( ROGUE_ARG(THIS), ROGUE_ARG(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[53] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], buildfile_2 ))) )))), Rogue_literal_strings[54] )))) )))) );
    if (ROGUE_COND(using_buildcore_3))
    {
      RogueGlobal__scan_config__File( ROGUE_ARG(THIS), ROGUE_ARG(((RogueClassFile*)(RogueFile__init__String( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassFile*,ROGUE_CREATE_OBJECT(File))), Rogue_literal_strings[27] )))) );
    }
    RogueGlobal__scan_config__File( ROGUE_ARG(THIS), ROGUE_ARG(((RogueClassFile*)(RogueFile__init__String( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassFile*,ROGUE_CREATE_OBJECT(File))), buildfile_2 )))) );
    ROGUE_DEF_LOCAL_REF(RogueString*,os_arg_5,(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[8] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], ROGUE_ARG(((RogueString*)(RogueString__to_lowercase( ROGUE_ARG((RogueSystem__os())) )))) ))) )))), Rogue_literal_strings[11] )))) )))));
    {
      ROGUE_DEF_LOCAL_REF(RogueString_List*,_auto_277_0,(((RogueString_List*)(RogueValueTable__keys( ROGUE_ARG(THIS->config) )))));
      RogueInt32 _auto_278_0 = (0);
      RogueInt32 _auto_279_0 = (((_auto_277_0->count) - (1)));
      for (;ROGUE_COND(((_auto_278_0) <= (_auto_279_0)));++_auto_278_0)
      {
        ROGUE_GC_CHECK;
        ROGUE_DEF_LOCAL_REF(RogueString*,key_0,(((RogueString*)(_auto_277_0->data->as_objects[_auto_278_0]))));
        if (ROGUE_COND(((RogueString__contains__String( key_0, os_arg_5 )))))
        {
          RogueValueTable__set__String_Value( ROGUE_ARG(THIS->config), ROGUE_ARG(((RogueString*)(RogueString__before_first__String( key_0, os_arg_5 )))), ROGUE_ARG(((RogueClassValue*)(RogueValueTable__get__String( ROGUE_ARG(THIS->config), key_0 )))) );
        }
      }
    }
    ROGUE_DEF_LOCAL_REF(RogueString*,roguec_args_6,(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)((RogueClassValue*)(RogueValueTable__get__String( ROGUE_ARG(THIS->config), Rogue_literal_strings[108] ))))) ))));
    ROGUE_DEF_LOCAL_REF(RogueString*,cmd_7,(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), ROGUE_ARG(((RogueString*)(RogueString__operatorPLUS__Object( ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], Rogue_literal_strings[0] ))), ROGUE_ARG(((RogueObject*)(((RogueClassValue*)(RogueValueTable__get__String( ROGUE_ARG(THIS->config), Rogue_literal_strings[16] )))))) )))) )))), Rogue_literal_strings[109] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], ROGUE_ARG((RogueSystem__os())) ))) )))), Rogue_literal_strings[110] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], buildfile_2 ))) )))) )))));
    if (ROGUE_COND(has_build_core_1))
    {
      cmd_7 = ((RogueString*)(((RogueString*)(RogueString__operatorPLUS__String( cmd_7, Rogue_literal_strings[111] )))));
    }
    cmd_7 = ((RogueString*)(((RogueString*)(RogueString__operatorPLUS__String( cmd_7, ROGUE_ARG(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[112] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], exe_4 ))) )))), Rogue_literal_strings[60] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], roguec_args_6 ))) )))) )))) )))));
    RogueGlobal__println__String( ROGUE_ARG(THIS), cmd_7 );
    if (ROGUE_COND(((0) != ((RogueSystem__run__String( cmd_7 ))))))
    {
      RogueConsoleErrorPrinter__println__String( ROGUE_ARG(((RogueClassConsole*)ROGUE_SINGLETON(Console))->error), ROGUE_ARG(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[113] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], buildfile_2 ))) )))), Rogue_literal_strings[2] )))) )))) );
      RogueSystem__exit__Int32( 1 );
    }
    cmd_7 = ((RogueString*)(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), ROGUE_ARG(((RogueString*)(RogueString__operatorPLUS__Object( ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], Rogue_literal_strings[0] ))), ROGUE_ARG(((RogueObject*)(((RogueClassValue*)(RogueValueTable__get__String( ROGUE_ARG(THIS->config), Rogue_literal_strings[26] )))))) )))) )))), Rogue_literal_strings[60] )))), ROGUE_ARG(((RogueString*)(RogueString__operatorPLUS__Object( ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], Rogue_literal_strings[0] ))), ROGUE_ARG(((RogueObject*)(((RogueClassValue*)(RogueValueTable__get__String( ROGUE_ARG(THIS->config), Rogue_literal_strings[114] )))))) )))) )))), Rogue_literal_strings[60] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], exe_4 ))) )))), Rogue_literal_strings[115] )))) )))));
    RogueLogical is_linux_8 = ((RogueString__operatorEQUALSEQUALS__String_String( ROGUE_ARG((RogueSystem__os())), Rogue_literal_strings[63] )));
    RogueGlobal__install_library_manager( ROGUE_ARG(THIS) );
    {
      ROGUE_DEF_LOCAL_REF(RogueClassValue*,_auto_280_0,(((RogueClassValue*)(RogueValueTable__get__String( ROGUE_ARG(THIS->config), Rogue_literal_strings[57] )))));
      RogueInt32 _auto_281_0 = (0);
      RogueInt32 _auto_282_0 = ((((Rogue_call_ROGUEM3( 22, _auto_280_0 ))) - (1)));
      for (;ROGUE_COND(((_auto_281_0) <= (_auto_282_0)));++_auto_281_0)
      {
        ROGUE_GC_CHECK;
        ROGUE_DEF_LOCAL_REF(RogueClassValue*,library_0,(((RogueClassValue*)Rogue_call_ROGUEM5( 32, _auto_280_0, _auto_281_0 ))));
        RogueGlobal__install_library__Value( ROGUE_ARG(THIS), library_0 );
        if (ROGUE_COND(!(is_linux_8)))
        {
          cmd_7 = ((RogueString*)(((RogueString*)(RogueString__operatorPLUS__String( cmd_7, ROGUE_ARG(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[122] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], ROGUE_ARG(((RogueString*)(RogueGlobal__header_location__Value( ROGUE_ARG(THIS), library_0 )))) ))) )))), Rogue_literal_strings[123] )))) )))) )))));
          cmd_7 = ((RogueString*)(((RogueString*)(RogueString__operatorPLUS__String( cmd_7, ROGUE_ARG(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[124] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], ROGUE_ARG(((RogueString*)(RogueGlobal__library_location__Value( ROGUE_ARG(THIS), library_0 )))) ))) )))), Rogue_literal_strings[123] )))) )))) )))));
        }
      }
    }
    cmd_7 = ((RogueString*)(((RogueString*)(RogueString__operatorPLUS__String( cmd_7, ROGUE_ARG(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[125] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], exe_4 ))) )))), Rogue_literal_strings[60] )))), ROGUE_ARG(((RogueString*)(RogueString__operatorPLUS__Object( ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], Rogue_literal_strings[0] ))), ROGUE_ARG(((RogueObject*)(((RogueClassValue*)(RogueValueTable__get__String( ROGUE_ARG(THIS->config), Rogue_literal_strings[126] )))))) )))) )))) )))) )))));
    RogueGlobal__println__String( ROGUE_ARG(THIS), cmd_7 );
    if (ROGUE_COND(((0) != ((RogueSystem__run__String( cmd_7 ))))))
    {
      RogueSystem__exit__Int32( 1 );
    }
  }
  RogueSystem__exit__Int32( ROGUE_ARG((RogueSystem__run__String( ROGUE_ARG(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], exe_4 ))) )))), Rogue_literal_strings[60] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], ROGUE_ARG(((RogueString*)(RogueString_List__join__String( ROGUE_ARG(((RogueString_List*)(RogueString_List__mapped_String____Function_String_RETURNSString_( ROGUE_ARG(RogueSystem_command_line_arguments), ROGUE_ARG(((RogueClass_Function_String_RETURNSString_*)(((RogueClassFunction_283*)ROGUE_SINGLETON(Function_283))))) )))), Rogue_literal_strings[60] )))) ))) )))) )))) ))) );
}

void RogueGlobal__run_tests( RogueClassGlobal* THIS )
{
}

void RogueGlobal__call_exit_functions( RogueClassGlobal* THIS )
{
  ROGUE_GC_CHECK;
  ROGUE_DEF_LOCAL_REF(Rogue_Function____List*,functions_0,(THIS->exit_functions));
  THIS->exit_functions = ((Rogue_Function____List*)(NULL));
  if (ROGUE_COND(!!(functions_0)))
  {
    {
      ROGUE_DEF_LOCAL_REF(Rogue_Function____List*,_auto_284_0,(functions_0));
      RogueInt32 _auto_285_0 = (0);
      RogueInt32 _auto_286_0 = (((_auto_284_0->count) - (1)));
      for (;ROGUE_COND(((_auto_285_0) <= (_auto_286_0)));++_auto_285_0)
      {
        ROGUE_GC_CHECK;
        ROGUE_DEF_LOCAL_REF(RogueClass_Function___*,fn_0,(((RogueClass_Function___*)(_auto_284_0->data->as_objects[_auto_285_0]))));
        Rogue_call_ROGUEM0( 13, fn_0 );
      }
    }
  }
}

void RogueGlobal__on_exit___Function___( RogueClassGlobal* THIS, RogueClass_Function___* fn_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(!(!!(THIS->exit_functions))))
  {
    THIS->exit_functions = ((Rogue_Function____List*)(Rogue_Function____List__init( ROGUE_ARG(ROGUE_CREATE_REF(Rogue_Function____List*,ROGUE_CREATE_OBJECT(_Function____List))) )));
  }
  Rogue_Function____List__add___Function___( ROGUE_ARG(THIS->exit_functions), fn_0 );
}

RogueClassPrintWriter_global_output_buffer_* RoguePrintWriter_global_output_buffer___close( RogueObject* THIS )
{
  switch (THIS->type->index)
  {
    case 1:
      return (RogueClassPrintWriter_global_output_buffer_*)RogueGlobal__close( (RogueClassGlobal*)THIS );
    default:
      return 0;
  }
}

RogueClassPrintWriter_global_output_buffer_* RoguePrintWriter_global_output_buffer___flush( RogueObject* THIS )
{
  switch (THIS->type->index)
  {
    case 1:
      return (RogueClassPrintWriter_global_output_buffer_*)RogueGlobal__flush( (RogueClassGlobal*)THIS );
    default:
      return 0;
  }
}

RogueClassPrintWriter_global_output_buffer_* RoguePrintWriter_global_output_buffer___println__String( RogueObject* THIS, RogueString* value_0 )
{
  switch (THIS->type->index)
  {
    case 1:
      return (RogueClassPrintWriter_global_output_buffer_*)RogueGlobal__println__String( (RogueClassGlobal*)THIS, value_0 );
    default:
      return 0;
  }
}

RogueClassPrintWriter_global_output_buffer_* RoguePrintWriter_global_output_buffer___write__StringBuilder( RogueObject* THIS, RogueStringBuilder* buffer_0 )
{
  switch (THIS->type->index)
  {
    case 1:
      return (RogueClassPrintWriter_global_output_buffer_*)RogueGlobal__write__StringBuilder( (RogueClassGlobal*)THIS, buffer_0 );
    default:
      return 0;
  }
}

RogueClassPrintWriter* RoguePrintWriter__close( RogueObject* THIS )
{
  switch (THIS->type->index)
  {
    case 1:
      return (RogueClassPrintWriter*)RogueGlobal__close( (RogueClassGlobal*)THIS );
    case 45:
      return (RogueClassPrintWriter*)RogueConsole__close( (RogueClassConsole*)THIS );
    case 47:
      return (RogueClassPrintWriter*)RogueConsoleErrorPrinter__close( (RogueClassConsoleErrorPrinter*)THIS );
    case 65:
      return (RogueClassPrintWriter*)RoguePrintWriterAdapter__close( (RogueClassPrintWriterAdapter*)THIS );
    default:
      return 0;
  }
}

RogueClassPrintWriter* RoguePrintWriter__flush( RogueObject* THIS )
{
  switch (THIS->type->index)
  {
    case 1:
      return (RogueClassPrintWriter*)RogueGlobal__flush( (RogueClassGlobal*)THIS );
    case 45:
      return (RogueClassPrintWriter*)RogueConsole__flush( (RogueClassConsole*)THIS );
    case 47:
      return (RogueClassPrintWriter*)RogueConsoleErrorPrinter__flush( (RogueClassConsoleErrorPrinter*)THIS );
    case 65:
      return (RogueClassPrintWriter*)RoguePrintWriterAdapter__flush( (RogueClassPrintWriterAdapter*)THIS );
    default:
      return 0;
  }
}

RogueClassPrintWriter* RoguePrintWriter__println__String( RogueObject* THIS, RogueString* value_0 )
{
  switch (THIS->type->index)
  {
    case 1:
      return (RogueClassPrintWriter*)RogueGlobal__println__String( (RogueClassGlobal*)THIS, value_0 );
    case 45:
      return (RogueClassPrintWriter*)RogueConsole__println__String( (RogueClassConsole*)THIS, value_0 );
    case 47:
      return (RogueClassPrintWriter*)RogueConsoleErrorPrinter__println__String( (RogueClassConsoleErrorPrinter*)THIS, value_0 );
    case 65:
      return (RogueClassPrintWriter*)RoguePrintWriterAdapter__println__String( (RogueClassPrintWriterAdapter*)THIS, value_0 );
    default:
      return 0;
  }
}

RogueClassPrintWriter* RoguePrintWriter__write__StringBuilder( RogueObject* THIS, RogueStringBuilder* buffer_0 )
{
  switch (THIS->type->index)
  {
    case 1:
      return (RogueClassPrintWriter*)RogueGlobal__write__StringBuilder( (RogueClassGlobal*)THIS, buffer_0 );
    case 45:
      return (RogueClassPrintWriter*)RogueConsole__write__StringBuilder( (RogueClassConsole*)THIS, buffer_0 );
    case 47:
      return (RogueClassPrintWriter*)RogueConsoleErrorPrinter__write__StringBuilder( (RogueClassConsoleErrorPrinter*)THIS, buffer_0 );
    case 65:
      return (RogueClassPrintWriter*)RoguePrintWriterAdapter__write__StringBuilder( (RogueClassPrintWriterAdapter*)THIS, buffer_0 );
    default:
      return 0;
  }
}

RogueClassValueTable* RogueValueTable__init_object( RogueClassValueTable* THIS )
{
  ROGUE_GC_CHECK;
  RogueValue__init_object( ROGUE_ARG(((RogueClassValue*)THIS)) );
  return (RogueClassValueTable*)(THIS);
}

RogueClassValueTable* RogueValueTable__init( RogueClassValueTable* THIS )
{
  ROGUE_GC_CHECK;
  THIS->data = ((RogueClassTable_String_Value_*)(RogueTable_String_Value___init( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassTable_String_Value_*,ROGUE_CREATE_OBJECT(Table_String_Value_))) )));
  return (RogueClassValueTable*)(THIS);
}

RogueString* RogueValueTable__to_String( RogueClassValueTable* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(((RogueString*)(RogueTable_String_Value___to_String( ROGUE_ARG(THIS->data) ))));
}

RogueString* RogueValueTable__type_name( RogueClassValueTable* THIS )
{
  return (RogueString*)(Rogue_literal_strings[167]);
}

RogueLogical RogueValueTable__contains__String( RogueClassValueTable* THIS, RogueString* key_0 )
{
  ROGUE_GC_CHECK;
  return (RogueLogical)(((RogueTable_String_Value___contains__String( ROGUE_ARG(THIS->data), key_0 ))));
}

RogueLogical RogueValueTable__contains__Value( RogueClassValueTable* THIS, RogueClassValue* key_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((!((RogueOptionalValue__operator__Value( key_0 )))) || (!((Rogue_call_ROGUEM6( 49, key_0 )))))))
  {
    return (RogueLogical)(false);
  }
  return (RogueLogical)(((RogueValueTable__contains__String( ROGUE_ARG(THIS), ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ((RogueObject*)key_0) ))) ))));
}

RogueInt32 RogueValueTable__count( RogueClassValueTable* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueInt32)(THIS->data->count);
}

RogueClassValueList* RogueValueTable__ensure_list__String( RogueClassValueTable* THIS, RogueString* key_0 )
{
  ROGUE_GC_CHECK;
  ROGUE_DEF_LOCAL_REF(RogueClassValueList*,list_1,(((RogueClassValueList*)(RogueObject_as(((RogueClassValue*)(RogueValueTable__get__String( ROGUE_ARG(THIS), key_0 ))),RogueTypeValueList)))));
  if (ROGUE_COND((RogueOptionalValue__operator__Value( ROGUE_ARG(((RogueClassValue*)(list_1))) ))))
  {
    return (RogueClassValueList*)(list_1);
  }
  list_1 = ((RogueClassValueList*)(((RogueClassValueList*)(RogueValueList__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassValueList*,ROGUE_CREATE_OBJECT(ValueList))) )))));
  RogueValueTable__set__String_Value( ROGUE_ARG(THIS), key_0, ROGUE_ARG(((RogueClassValue*)(list_1))) );
  return (RogueClassValueList*)(list_1);
}

RogueClassValueTable* RogueValueTable__ensure_table__String( RogueClassValueTable* THIS, RogueString* key_0 )
{
  ROGUE_GC_CHECK;
  ROGUE_DEF_LOCAL_REF(RogueClassValueTable*,table_1,(((RogueClassValueTable*)(RogueObject_as(((RogueClassValue*)(RogueValueTable__get__String( ROGUE_ARG(THIS), key_0 ))),RogueTypeValueTable)))));
  if (ROGUE_COND((RogueOptionalValue__operator__Value( ROGUE_ARG(((RogueClassValue*)(table_1))) ))))
  {
    return (RogueClassValueTable*)(table_1);
  }
  ROGUE_DEF_LOCAL_REF(RogueClassValueTable*,_auto_357_0,(((RogueClassValueTable*)(RogueValueTable__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassValueTable*,ROGUE_CREATE_OBJECT(ValueTable))) )))));
  {
  }
  table_1 = ((RogueClassValueTable*)(_auto_357_0));
  RogueValueTable__set__String_Value( ROGUE_ARG(THIS), key_0, ROGUE_ARG(((RogueClassValue*)(table_1))) );
  return (RogueClassValueTable*)(table_1);
}

RogueClassValue* RogueValueTable__get__Int32( RogueClassValueTable* THIS, RogueInt32 index_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((((index_0) < (0))) || (((index_0) >= (((RogueValueTable__count( ROGUE_ARG(THIS) )))))))))
  {
    return (RogueClassValue*)(((RogueClassValue*)(((RogueClassUndefinedValue*)ROGUE_SINGLETON(UndefinedValue)))));
  }
  ROGUE_DEF_LOCAL_REF(RogueClassValue*,result_1,(((RogueClassValue*)(RogueTable_String_Value___get__String( ROGUE_ARG(THIS->data), ROGUE_ARG(((RogueString*)(((RogueString_List*)(RogueValueTable__keys( ROGUE_ARG(THIS) )))->data->as_objects[index_0]))) )))));
  if (ROGUE_COND(((void*)result_1) == ((void*)NULL)))
  {
    return (RogueClassValue*)(((RogueClassValue*)(((RogueClassUndefinedValue*)ROGUE_SINGLETON(UndefinedValue)))));
  }
  return (RogueClassValue*)(result_1);
}

RogueClassValue* RogueValueTable__get__String( RogueClassValueTable* THIS, RogueString* key_0 )
{
  ROGUE_GC_CHECK;
  ROGUE_DEF_LOCAL_REF(RogueClassValue*,result_1,(((RogueClassValue*)(RogueTable_String_Value___get__String( ROGUE_ARG(THIS->data), key_0 )))));
  if (ROGUE_COND(((void*)result_1) == ((void*)NULL)))
  {
    return (RogueClassValue*)(((RogueClassValue*)(((RogueClassUndefinedValue*)ROGUE_SINGLETON(UndefinedValue)))));
  }
  return (RogueClassValue*)(result_1);
}

RogueLogical RogueValueTable__is_collection( RogueClassValueTable* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueLogical)(true);
}

RogueString_List* RogueValueTable__keys( RogueClassValueTable* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueString_List*)(((RogueString_List*)(RogueTable_String_Value___keys__String_List( ROGUE_ARG(THIS->data), ROGUE_ARG(((RogueString_List*)(NULL))) ))));
}

RogueClassValueTable* RogueValueTable__set__String_Value( RogueClassValueTable* THIS, RogueString* key_0, RogueClassValue* new_value_1 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((void*)new_value_1) == ((void*)NULL)))
  {
    new_value_1 = ((RogueClassValue*)(((RogueClassValue*)(((RogueClassNullValue*)ROGUE_SINGLETON(NullValue))))));
  }
  RogueTable_String_Value___set__String_Value( ROGUE_ARG(THIS->data), key_0, new_value_1 );
  return (RogueClassValueTable*)(THIS);
}

RogueLogical RogueValueTable__to_Logical( RogueClassValueTable* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueLogical)(true);
}

RogueStringBuilder* RogueValueTable__to_json__StringBuilder_Int32( RogueClassValueTable* THIS, RogueStringBuilder* buffer_0, RogueInt32 flags_1 )
{
  ROGUE_GC_CHECK;
  RogueLogical pretty_print_2 = (((!!(((flags_1) & (1)))) && (((((RogueValue__is_complex( ROGUE_ARG(((RogueClassValue*)THIS)) )))) || (!!(((flags_1) & (2))))))));
  RogueStringBuilder__print__Character_Logical( buffer_0, (RogueCharacter)'{', true );
  if (ROGUE_COND(pretty_print_2))
  {
    RogueStringBuilder__println( buffer_0 );
    buffer_0->indent += 2;
  }
  RogueLogical first_3 = (true);
  {
    ROGUE_DEF_LOCAL_REF(RogueString_List*,_auto_377_0,(((RogueString_List*)(RogueTable_String_Value___keys__String_List( ROGUE_ARG(THIS->data), ROGUE_ARG(((RogueString_List*)(NULL))) )))));
    RogueInt32 _auto_378_0 = (0);
    RogueInt32 _auto_379_0 = (((_auto_377_0->count) - (1)));
    for (;ROGUE_COND(((_auto_378_0) <= (_auto_379_0)));++_auto_378_0)
    {
      ROGUE_GC_CHECK;
      ROGUE_DEF_LOCAL_REF(RogueString*,key_0,(((RogueString*)(_auto_377_0->data->as_objects[_auto_378_0]))));
      if (ROGUE_COND(first_3))
      {
        first_3 = ((RogueLogical)(false));
      }
      else
      {
        if (ROGUE_COND(!(!!(((flags_1) & (2))))))
        {
          RogueStringBuilder__print__Character_Logical( buffer_0, (RogueCharacter)',', true );
        }
        if (ROGUE_COND(pretty_print_2))
        {
          RogueStringBuilder__println( buffer_0 );
        }
      }
      RogueStringValue__to_json__String_StringBuilder_Int32( key_0, buffer_0, flags_1 );
      RogueStringBuilder__print__Character_Logical( buffer_0, (RogueCharacter)':', true );
      RogueLogical indent_4 = (false);
      ROGUE_DEF_LOCAL_REF(RogueClassValue*,value_5,(((RogueClassValue*)(RogueTable_String_Value___get__String( ROGUE_ARG(THIS->data), key_0 )))));
      if (ROGUE_COND(((pretty_print_2) && ((((((RogueOptionalValue__operator__Value( value_5 ))) && (((RogueValue__is_complex( value_5 )))))) || (!!(((flags_1) & (2)))))))))
      {
        RogueStringBuilder__println( buffer_0 );
        indent_4 = ((RogueLogical)(((((void*)value_5) == ((void*)NULL)) || (!((Rogue_call_ROGUEM6( 38, value_5 )))))));
        if (ROGUE_COND(indent_4))
        {
          buffer_0->indent += 2;
        }
      }
      if (ROGUE_COND(((((void*)value_5) != ((void*)NULL)) && ((((RogueOptionalValue__operator__Value( value_5 ))) || ((Rogue_call_ROGUEM6( 43, value_5 ))))))))
      {
        Rogue_call_ROGUEM10( 115, value_5, buffer_0, flags_1 );
      }
      else
      {
        RogueStringBuilder__print__String( buffer_0, Rogue_literal_strings[1] );
      }
      if (ROGUE_COND(indent_4))
      {
        buffer_0->indent -= 2;
      }
    }
  }
  if (ROGUE_COND(pretty_print_2))
  {
    RogueStringBuilder__println( buffer_0 );
    buffer_0->indent -= 2;
  }
  RogueStringBuilder__print__Character_Logical( buffer_0, (RogueCharacter)'}', true );
  return (RogueStringBuilder*)(buffer_0);
}

RogueClassValue* RogueValue__init_object( RogueClassValue* THIS )
{
  ROGUE_GC_CHECK;
  RogueObject__init_object( ROGUE_ARG(((RogueObject*)THIS)) );
  return (RogueClassValue*)(THIS);
}

RogueString* RogueValue__type_name( RogueClassValue* THIS )
{
  return (RogueString*)(Rogue_literal_strings[135]);
}

RogueLogical RogueValue__contains__String( RogueClassValue* THIS, RogueString* table_key_or_list_value_0 )
{
  ROGUE_GC_CHECK;
  return (RogueLogical)(false);
}

RogueLogical RogueValue__contains__Value( RogueClassValue* THIS, RogueClassValue* table_key_or_list_value_0 )
{
  ROGUE_GC_CHECK;
  return (RogueLogical)(false);
}

RogueInt32 RogueValue__count( RogueClassValue* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueInt32)(0);
}

RogueClassValueTable* RogueValue__ensure_table__String( RogueClassValue* THIS, RogueString* key_0 )
{
  ROGUE_GC_CHECK;
  ROGUE_DEF_LOCAL_REF(RogueClassValueTable*,_auto_340_0,(((RogueClassValueTable*)(RogueValueTable__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassValueTable*,ROGUE_CREATE_OBJECT(ValueTable))) )))));
  {
  }
  return (RogueClassValueTable*)(_auto_340_0);
}

RogueClassValue* RogueValue__get__Int32( RogueClassValue* THIS, RogueInt32 index_0 )
{
  ROGUE_GC_CHECK;
  return (RogueClassValue*)(((RogueClassValue*)(((RogueClassUndefinedValue*)ROGUE_SINGLETON(UndefinedValue)))));
}

RogueClassValue* RogueValue__get__String( RogueClassValue* THIS, RogueString* key_0 )
{
  ROGUE_GC_CHECK;
  return (RogueClassValue*)(((RogueClassValue*)(((RogueClassUndefinedValue*)ROGUE_SINGLETON(UndefinedValue)))));
}

RogueLogical RogueValue__is_collection( RogueClassValue* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueLogical)(false);
}

RogueLogical RogueValue__is_complex( RogueClassValue* THIS )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(!((Rogue_call_ROGUEM6( 38, ROGUE_ARG(THIS) )))))
  {
    return (RogueLogical)(false);
  }
  if (ROGUE_COND((((Rogue_call_ROGUEM3( 22, ROGUE_ARG(THIS) ))) > (1))))
  {
    return (RogueLogical)(true);
  }
  {
    ROGUE_DEF_LOCAL_REF(RogueClassValue*,_auto_343_0,(THIS));
    RogueInt32 _auto_344_0 = (0);
    RogueInt32 _auto_345_0 = ((((Rogue_call_ROGUEM3( 22, _auto_343_0 ))) - (1)));
    for (;ROGUE_COND(((_auto_344_0) <= (_auto_345_0)));++_auto_344_0)
    {
      ROGUE_GC_CHECK;
      ROGUE_DEF_LOCAL_REF(RogueClassValue*,value_0,(((RogueClassValue*)Rogue_call_ROGUEM5( 32, _auto_343_0, _auto_344_0 ))));
      if (ROGUE_COND((((RogueOptionalValue__operator__Value( value_0 ))) && (((RogueValue__is_complex( value_0 )))))))
      {
        return (RogueLogical)(true);
      }
    }
  }
  return (RogueLogical)(false);
}

RogueLogical RogueValue__is_logical( RogueClassValue* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueLogical)(false);
}

RogueLogical RogueValue__is_null( RogueClassValue* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueLogical)(false);
}

RogueLogical RogueValue__is_non_null( RogueClassValue* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueLogical)(true);
}

RogueLogical RogueValue__is_number( RogueClassValue* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueLogical)(false);
}

RogueLogical RogueValue__is_string( RogueClassValue* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueLogical)(false);
}

RogueLogical RogueValue__operatorEQUALSEQUALS__Value( RogueClassValue* THIS, RogueClassValue* other_0 )
{
  ROGUE_GC_CHECK;
  return (RogueLogical)(((void*)THIS) == ((void*)other_0));
}

RogueLogical RogueValue__operatorEQUALSEQUALS__String( RogueClassValue* THIS, RogueString* other_0 )
{
  ROGUE_GC_CHECK;
  return (RogueLogical)((Rogue_call_ROGUEM2( 59, ROGUE_ARG(THIS), ROGUE_ARG((RogueValue__create__String( other_0 ))) )));
}

RogueClassValue* RogueValue__remove__Value( RogueClassValue* THIS, RogueClassValue* value_0 )
{
  ROGUE_GC_CHECK;
  return (RogueClassValue*)(value_0);
}

RogueLogical RogueValue__save__File_Logical_Logical( RogueClassValue* THIS, RogueClassFile* file_0, RogueLogical formatted_1, RogueLogical omit_commas_2 )
{
  ROGUE_GC_CHECK;
  return (RogueLogical)((RogueFile__save__String_String( ROGUE_ARG(file_0->filepath), ROGUE_ARG(((RogueString*)(RogueValue__to_json__Logical_Logical( ROGUE_ARG(THIS), formatted_1, omit_commas_2 )))) )));
}

RogueClassValue* RogueValue__set__String_Value( RogueClassValue* THIS, RogueString* key_0, RogueClassValue* value_1 )
{
  ROGUE_GC_CHECK;
  return (RogueClassValue*)(THIS);
}

RogueInt64 RogueValue__to_Int64( RogueClassValue* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueInt64)(0LL);
}

RogueInt32 RogueValue__to_Int32( RogueClassValue* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueInt32)(((RogueInt32)((Rogue_call_ROGUEM8( 106, ROGUE_ARG(THIS) )))));
}

RogueLogical RogueValue__to_Logical( RogueClassValue* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueLogical)((RogueLogical__create__Int32( ROGUE_ARG((Rogue_call_ROGUEM3( 107, ROGUE_ARG(THIS) ))) )));
}

RogueReal64 RogueValue__to_Real64( RogueClassValue* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueReal64)(((RogueReal64)((Rogue_call_ROGUEM3( 107, ROGUE_ARG(THIS) )))));
}

RogueString* RogueValue__to_json__Logical_Logical( RogueClassValue* THIS, RogueLogical formatted_0, RogueLogical omit_commas_1 )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueValue__to_json__StringBuilder_Logical_Logical( ROGUE_ARG(THIS), ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), formatted_0, omit_commas_1 )))) ))));
}

RogueStringBuilder* RogueValue__to_json__StringBuilder_Logical_Logical( RogueClassValue* THIS, RogueStringBuilder* buffer_0, RogueLogical formatted_1, RogueLogical omit_commas_2 )
{
  ROGUE_GC_CHECK;
  RogueInt32 flags_3 = (0);
  if (ROGUE_COND(formatted_1))
  {
    flags_3 |= 1;
  }
  if (ROGUE_COND(omit_commas_2))
  {
    flags_3 |= 3;
  }
  return (RogueStringBuilder*)(((RogueStringBuilder*)Rogue_call_ROGUEM10( 115, ROGUE_ARG(THIS), buffer_0, flags_3 )));
}

RogueStringBuilder* RogueValue__to_json__StringBuilder_Int32( RogueClassValue* THIS, RogueStringBuilder* buffer_0, RogueInt32 flags_1 )
{
  ROGUE_GC_CHECK;
  return (RogueStringBuilder*)(buffer_0);
}

RogueClassTable_String_Value_* RogueTable_String_Value___init_object( RogueClassTable_String_Value_* THIS )
{
  ROGUE_GC_CHECK;
  RogueObject__init_object( ROGUE_ARG(((RogueObject*)THIS)) );
  return (RogueClassTable_String_Value_*)(THIS);
}

RogueClassTable_String_Value_* RogueTable_String_Value___init( RogueClassTable_String_Value_* THIS )
{
  ROGUE_GC_CHECK;
  RogueTable_String_Value___init__Int32( ROGUE_ARG(THIS), 16 );
  return (RogueClassTable_String_Value_*)(THIS);
}

RogueString* RogueTable_String_Value___to_String( RogueClassTable_String_Value_* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueTable_String_Value___print_to__StringBuilder( ROGUE_ARG(THIS), ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))) )))) ))));
}

RogueString* RogueTable_String_Value___type_name( RogueClassTable_String_Value_* THIS )
{
  return (RogueString*)(Rogue_literal_strings[151]);
}

RogueClassTable_String_Value_* RogueTable_String_Value___init__Int32( RogueClassTable_String_Value_* THIS, RogueInt32 bin_count_0 )
{
  ROGUE_GC_CHECK;
  RogueInt32 bins_power_of_2_1 = (1);
  while (ROGUE_COND(((bins_power_of_2_1) < (bin_count_0))))
  {
    ROGUE_GC_CHECK;
    bins_power_of_2_1 = ((RogueInt32)(((bins_power_of_2_1) << (1))));
  }
  bin_count_0 = ((RogueInt32)(bins_power_of_2_1));
  THIS->bin_mask = ((bin_count_0) - (1));
  THIS->bins = RogueType_create_array( bin_count_0, sizeof(RogueClassTableEntry_String_Value_*), true, 10 );
  return (RogueClassTable_String_Value_*)(THIS);
}

RogueLogical RogueTable_String_Value___contains__String( RogueClassTable_String_Value_* THIS, RogueString* key_0 )
{
  ROGUE_GC_CHECK;
  return (RogueLogical)(!!(((RogueClassTableEntry_String_Value_*)(RogueTable_String_Value___find__String( ROGUE_ARG(THIS), key_0 )))));
}

RogueClassTableEntry_String_Value_* RogueTable_String_Value___find__String( RogueClassTable_String_Value_* THIS, RogueString* key_0 )
{
  ROGUE_GC_CHECK;
  RogueInt32 hash_1 = (((RogueString__hash_code( key_0 ))));
  ROGUE_DEF_LOCAL_REF(RogueClassTableEntry_String_Value_*,entry_2,(((RogueClassTableEntry_String_Value_*)(THIS->bins->as_objects[((hash_1) & (THIS->bin_mask))]))));
  while (ROGUE_COND(!!(entry_2)))
  {
    ROGUE_GC_CHECK;
    if (ROGUE_COND(((((entry_2->hash) == (hash_1))) && ((RogueString__operatorEQUALSEQUALS__String_String( ROGUE_ARG(entry_2->key), key_0 ))))))
    {
      return (RogueClassTableEntry_String_Value_*)(entry_2);
    }
    entry_2 = ((RogueClassTableEntry_String_Value_*)(entry_2->adjacent_entry));
  }
  return (RogueClassTableEntry_String_Value_*)(((RogueClassTableEntry_String_Value_*)(NULL)));
}

RogueClassValue* RogueTable_String_Value___get__String( RogueClassTable_String_Value_* THIS, RogueString* key_0 )
{
  ROGUE_GC_CHECK;
  ROGUE_DEF_LOCAL_REF(RogueClassTableEntry_String_Value_*,entry_1,(((RogueClassTableEntry_String_Value_*)(RogueTable_String_Value___find__String( ROGUE_ARG(THIS), key_0 )))));
  if (ROGUE_COND(!!(entry_1)))
  {
    return (RogueClassValue*)(entry_1->value);
  }
  else
  {
    ROGUE_DEF_LOCAL_REF(RogueClassValue*,default_value_2,0);
    return (RogueClassValue*)(default_value_2);
  }
}

RogueString_List* RogueTable_String_Value___keys__String_List( RogueClassTable_String_Value_* THIS, RogueString_List* list_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(!(!!(list_0))))
  {
    list_0 = ((RogueString_List*)(((RogueString_List*)(RogueString_List__init__Int32( ROGUE_ARG(ROGUE_CREATE_REF(RogueString_List*,ROGUE_CREATE_OBJECT(String_List))), ROGUE_ARG(THIS->count) )))));
  }
  RogueString_List__reserve__Int32( list_0, ROGUE_ARG(THIS->count) );
  ROGUE_DEF_LOCAL_REF(RogueClassTableEntry_String_Value_*,cur_1,(THIS->first_entry));
  while (ROGUE_COND(!!(cur_1)))
  {
    ROGUE_GC_CHECK;
    RogueString_List__add__String( list_0, ROGUE_ARG(cur_1->key) );
    cur_1 = ((RogueClassTableEntry_String_Value_*)(cur_1->next_entry));
  }
  return (RogueString_List*)(list_0);
}

RogueStringBuilder* RogueTable_String_Value___print_to__StringBuilder( RogueClassTable_String_Value_* THIS, RogueStringBuilder* buffer_0 )
{
  ROGUE_GC_CHECK;
  RogueStringBuilder__print__Character_Logical( buffer_0, (RogueCharacter)'{', true );
  ROGUE_DEF_LOCAL_REF(RogueClassTableEntry_String_Value_*,cur_1,(THIS->first_entry));
  RogueInt32 i_2 = (0);
  while (ROGUE_COND(!!(cur_1)))
  {
    ROGUE_GC_CHECK;
    if (ROGUE_COND(((i_2) > (0))))
    {
      RogueStringBuilder__print__Character_Logical( buffer_0, (RogueCharacter)',', true );
    }
    RogueStringBuilder__print__String( buffer_0, ROGUE_ARG(cur_1->key) );
    RogueStringBuilder__print__Character_Logical( buffer_0, (RogueCharacter)':', true );
    RogueStringBuilder__print__Object( buffer_0, ROGUE_ARG(((RogueObject*)(cur_1->value))) );
    cur_1 = ((RogueClassTableEntry_String_Value_*)(cur_1->next_entry));
    ++i_2;
  }
  RogueStringBuilder__print__Character_Logical( buffer_0, (RogueCharacter)'}', true );
  return (RogueStringBuilder*)(buffer_0);
}

RogueClassTable_String_Value_* RogueTable_String_Value___set__String_Value( RogueClassTable_String_Value_* THIS, RogueString* key_0, RogueClassValue* value_1 )
{
  ROGUE_GC_CHECK;
  ROGUE_DEF_LOCAL_REF(RogueClassTableEntry_String_Value_*,entry_2,(((RogueClassTableEntry_String_Value_*)(RogueTable_String_Value___find__String( ROGUE_ARG(THIS), key_0 )))));
  if (ROGUE_COND(!!(entry_2)))
  {
    entry_2->value = value_1;
    if (ROGUE_COND(!!(THIS->sort_function)))
    {
      RogueTable_String_Value____adjust_entry_order__TableEntry_String_Value_( ROGUE_ARG(THIS), entry_2 );
    }
    return (RogueClassTable_String_Value_*)(THIS);
  }
  if (ROGUE_COND(((THIS->count) >= (THIS->bins->count))))
  {
    RogueTable_String_Value____grow( ROGUE_ARG(THIS) );
  }
  RogueInt32 hash_3 = (((RogueString__hash_code( key_0 ))));
  RogueInt32 index_4 = (((hash_3) & (THIS->bin_mask)));
  if (ROGUE_COND(!(!!(entry_2))))
  {
    entry_2 = ((RogueClassTableEntry_String_Value_*)(((RogueClassTableEntry_String_Value_*)(RogueTableEntry_String_Value___init__String_Value_Int32( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassTableEntry_String_Value_*,ROGUE_CREATE_OBJECT(TableEntry_String_Value_))), key_0, value_1, hash_3 )))));
  }
  entry_2->adjacent_entry = ((RogueClassTableEntry_String_Value_*)(THIS->bins->as_objects[index_4]));
  THIS->bins->as_objects[index_4] = entry_2;
  RogueTable_String_Value____place_entry_in_order__TableEntry_String_Value_( ROGUE_ARG(THIS), entry_2 );
  THIS->count = ((THIS->count) + (1));
  return (RogueClassTable_String_Value_*)(THIS);
}

void RogueTable_String_Value____adjust_entry_order__TableEntry_String_Value_( RogueClassTable_String_Value_* THIS, RogueClassTableEntry_String_Value_* entry_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((void*)THIS->first_entry) == ((void*)THIS->last_entry)))
  {
    return;
  }
  if (ROGUE_COND(((void*)entry_0) == ((void*)THIS->first_entry)))
  {
    if (ROGUE_COND((Rogue_call_ROGUEM11( 13, ROGUE_ARG(THIS->sort_function), entry_0, ROGUE_ARG(entry_0->next_entry) ))))
    {
      return;
    }
  }
  else if (ROGUE_COND(((void*)entry_0) == ((void*)THIS->last_entry)))
  {
    if (ROGUE_COND((Rogue_call_ROGUEM11( 13, ROGUE_ARG(THIS->sort_function), ROGUE_ARG(entry_0->previous_entry), entry_0 ))))
    {
      return;
    }
  }
  else if (ROGUE_COND((((Rogue_call_ROGUEM11( 13, ROGUE_ARG(THIS->sort_function), ROGUE_ARG(entry_0->previous_entry), entry_0 ))) && ((Rogue_call_ROGUEM11( 13, ROGUE_ARG(THIS->sort_function), entry_0, ROGUE_ARG(entry_0->next_entry) ))))))
  {
    return;
  }
  RogueTable_String_Value____unlink__TableEntry_String_Value_( ROGUE_ARG(THIS), entry_0 );
  RogueTable_String_Value____place_entry_in_order__TableEntry_String_Value_( ROGUE_ARG(THIS), entry_0 );
}

void RogueTable_String_Value____place_entry_in_order__TableEntry_String_Value_( RogueClassTable_String_Value_* THIS, RogueClassTableEntry_String_Value_* entry_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(!!(THIS->first_entry)))
  {
    if (ROGUE_COND(!!(THIS->sort_function)))
    {
      if (ROGUE_COND((Rogue_call_ROGUEM11( 13, ROGUE_ARG(THIS->sort_function), entry_0, ROGUE_ARG(THIS->first_entry) ))))
      {
        entry_0->next_entry = THIS->first_entry;
        THIS->first_entry->previous_entry = entry_0;
        THIS->first_entry = entry_0;
      }
      else if (ROGUE_COND((Rogue_call_ROGUEM11( 13, ROGUE_ARG(THIS->sort_function), ROGUE_ARG(THIS->last_entry), entry_0 ))))
      {
        THIS->last_entry->next_entry = entry_0;
        entry_0->previous_entry = THIS->last_entry;
        THIS->last_entry = entry_0;
      }
      else
      {
        ROGUE_DEF_LOCAL_REF(RogueClassTableEntry_String_Value_*,cur_1,(THIS->first_entry));
        while (ROGUE_COND(!!(cur_1->next_entry)))
        {
          ROGUE_GC_CHECK;
          if (ROGUE_COND((Rogue_call_ROGUEM11( 13, ROGUE_ARG(THIS->sort_function), entry_0, ROGUE_ARG(cur_1->next_entry) ))))
          {
            entry_0->previous_entry = cur_1;
            entry_0->next_entry = cur_1->next_entry;
            entry_0->next_entry->previous_entry = entry_0;
            cur_1->next_entry = entry_0;
            goto _auto_406;
          }
          cur_1 = ((RogueClassTableEntry_String_Value_*)(cur_1->next_entry));
        }
        _auto_406:;
      }
    }
    else
    {
      THIS->last_entry->next_entry = entry_0;
      entry_0->previous_entry = THIS->last_entry;
      THIS->last_entry = entry_0;
    }
  }
  else
  {
    THIS->first_entry = entry_0;
    THIS->last_entry = entry_0;
  }
}

void RogueTable_String_Value____unlink__TableEntry_String_Value_( RogueClassTable_String_Value_* THIS, RogueClassTableEntry_String_Value_* entry_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((void*)entry_0) == ((void*)THIS->first_entry)))
  {
    if (ROGUE_COND(((void*)entry_0) == ((void*)THIS->last_entry)))
    {
      THIS->first_entry = ((RogueClassTableEntry_String_Value_*)(NULL));
      THIS->last_entry = ((RogueClassTableEntry_String_Value_*)(NULL));
    }
    else
    {
      THIS->first_entry = entry_0->next_entry;
      THIS->first_entry->previous_entry = ((RogueClassTableEntry_String_Value_*)(NULL));
    }
  }
  else if (ROGUE_COND(((void*)entry_0) == ((void*)THIS->last_entry)))
  {
    THIS->last_entry = entry_0->previous_entry;
    THIS->last_entry->next_entry = ((RogueClassTableEntry_String_Value_*)(NULL));
  }
  else
  {
    entry_0->previous_entry->next_entry = entry_0->next_entry;
    entry_0->next_entry->previous_entry = entry_0->previous_entry;
  }
}

void RogueTable_String_Value____grow( RogueClassTable_String_Value_* THIS )
{
  ROGUE_GC_CHECK;
  THIS->bins = RogueType_create_array( ((THIS->bins->count) * (2)), sizeof(RogueClassTableEntry_String_Value_*), true, 10 );
  THIS->bin_mask = ((((THIS->bin_mask) << (1))) | (1));
  ROGUE_DEF_LOCAL_REF(RogueClassTableEntry_String_Value_*,cur_0,(THIS->first_entry));
  while (ROGUE_COND(!!(cur_0)))
  {
    ROGUE_GC_CHECK;
    RogueInt32 index_1 = (((cur_0->hash) & (THIS->bin_mask)));
    cur_0->adjacent_entry = ((RogueClassTableEntry_String_Value_*)(THIS->bins->as_objects[index_1]));
    THIS->bins->as_objects[index_1] = cur_0;
    cur_0 = ((RogueClassTableEntry_String_Value_*)(cur_0->next_entry));
  }
}

RogueInt32 RogueInt32__or_smaller__Int32( RogueInt32 THIS, RogueInt32 other_0 )
{
  ROGUE_GC_CHECK;
  return (RogueInt32)(((((((THIS) <= (other_0))))) ? (THIS) : other_0));
}

RogueCharacter RogueInt32__to_digit( RogueInt32 THIS )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((((THIS) >= (0))) && (((THIS) <= (9))))))
  {
    return (RogueCharacter)(((RogueCharacter)(((THIS) + (48)))));
  }
  if (ROGUE_COND(((((THIS) >= (10))) && (((THIS) <= (35))))))
  {
    return (RogueCharacter)(((RogueCharacter)(((((THIS) - (10))) + (65)))));
  }
  return (RogueCharacter)((RogueCharacter)'0');
}

RogueString* RogueArray_TableEntry_String_Value____type_name( RogueArray* THIS )
{
  return (RogueString*)(Rogue_literal_strings[187]);
}

RogueString* RogueArray__type_name( RogueArray* THIS )
{
  return (RogueString*)(Rogue_literal_strings[139]);
}

void RogueArray__zero__Int32_Int32( RogueArray* THIS, RogueInt32 i1_0, RogueInt32 n_1 )
{
  ROGUE_GC_CHECK;
  RogueInt32 size_2 = (THIS->element_size);
  memset( THIS->as_bytes + i1_0*size_2, 0, n_1*size_2 );

}

RogueClassTableEntry_String_Value_* RogueTableEntry_String_Value___init_object( RogueClassTableEntry_String_Value_* THIS )
{
  ROGUE_GC_CHECK;
  RogueObject__init_object( ROGUE_ARG(((RogueObject*)THIS)) );
  return (RogueClassTableEntry_String_Value_*)(THIS);
}

RogueString* RogueTableEntry_String_Value___to_String( RogueClassTableEntry_String_Value_* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[8] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], ROGUE_ARG(THIS->key) ))) )))), Rogue_literal_strings[76] )))), ROGUE_ARG(((RogueString*)(RogueString__operatorPLUS__Object( ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], Rogue_literal_strings[0] ))), ROGUE_ARG(((RogueObject*)(THIS->value))) )))) )))), Rogue_literal_strings[11] )))) ))));
}

RogueString* RogueTableEntry_String_Value___type_name( RogueClassTableEntry_String_Value_* THIS )
{
  return (RogueString*)(Rogue_literal_strings[158]);
}

RogueClassTableEntry_String_Value_* RogueTableEntry_String_Value___init__String_Value_Int32( RogueClassTableEntry_String_Value_* THIS, RogueString* _key_0, RogueClassValue* _value_1, RogueInt32 _hash_2 )
{
  ROGUE_GC_CHECK;
  THIS->key = _key_0;
  THIS->value = _value_1;
  THIS->hash = _hash_2;
  return (RogueClassTableEntry_String_Value_*)(THIS);
}

RogueInt32 RogueString__hash_code( RogueString* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueInt32)(((RogueInt32)(THIS->hash_code)));
}

RogueString* RogueString__to_String( RogueString* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(THIS);
}

RogueString* RogueString__type_name( RogueString* THIS )
{
  return (RogueString*)(Rogue_literal_strings[136]);
}

RogueString* RogueString__after__Int32( RogueString* THIS, RogueInt32 index_0 )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(((RogueString*)(RogueString__from__Int32( ROGUE_ARG(THIS), ROGUE_ARG(((index_0) + (1))) ))));
}

RogueString* RogueString__after_first__Character( RogueString* THIS, RogueCharacter ch_0 )
{
  ROGUE_GC_CHECK;
  RogueOptionalInt32 i_1 = (((RogueString__locate__Character_OptionalInt32( ROGUE_ARG(THIS), ch_0, (RogueOptionalInt32__create()) ))));
  if (ROGUE_COND(i_1.exists))
  {
    return (RogueString*)(((RogueString*)(RogueString__from__Int32( ROGUE_ARG(THIS), ROGUE_ARG(((i_1.value) + (1))) ))));
  }
  else
  {
    return (RogueString*)(Rogue_literal_strings[0]);
  }
}

RogueString* RogueString__after_first__String( RogueString* THIS, RogueString* st_0 )
{
  ROGUE_GC_CHECK;
  RogueOptionalInt32 i_1 = (((RogueString__locate__String_OptionalInt32( ROGUE_ARG(THIS), st_0, (RogueOptionalInt32__create()) ))));
  if (ROGUE_COND(i_1.exists))
  {
    return (RogueString*)(((RogueString*)(RogueString__from__Int32( ROGUE_ARG(THIS), ROGUE_ARG(((i_1.value) + (st_0->character_count))) ))));
  }
  else
  {
    return (RogueString*)(Rogue_literal_strings[0]);
  }
}

RogueString* RogueString__after_last__Character( RogueString* THIS, RogueCharacter ch_0 )
{
  ROGUE_GC_CHECK;
  RogueOptionalInt32 i_1 = (((RogueString__locate_last__Character_OptionalInt32( ROGUE_ARG(THIS), ch_0, (RogueOptionalInt32__create()) ))));
  if (ROGUE_COND(i_1.exists))
  {
    return (RogueString*)(((RogueString*)(RogueString__from__Int32( ROGUE_ARG(THIS), ROGUE_ARG(((i_1.value) + (1))) ))));
  }
  else
  {
    return (RogueString*)(Rogue_literal_strings[0]);
  }
}

RogueString* RogueString__before__Int32( RogueString* THIS, RogueInt32 index_0 )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(((RogueString*)(RogueString__from__Int32_Int32( ROGUE_ARG(THIS), 0, ROGUE_ARG(((index_0) - (1))) ))));
}

RogueString* RogueString__before_first__Character( RogueString* THIS, RogueCharacter ch_0 )
{
  ROGUE_GC_CHECK;
  RogueOptionalInt32 i_1 = (((RogueString__locate__Character_OptionalInt32( ROGUE_ARG(THIS), ch_0, (RogueOptionalInt32__create()) ))));
  if (ROGUE_COND(i_1.exists))
  {
    return (RogueString*)(((RogueString*)(RogueString__from__Int32_Int32( ROGUE_ARG(THIS), 0, ROGUE_ARG(((i_1.value) - (1))) ))));
  }
  else
  {
    return (RogueString*)(THIS);
  }
}

RogueString* RogueString__before_first__String( RogueString* THIS, RogueString* st_0 )
{
  ROGUE_GC_CHECK;
  RogueOptionalInt32 i_1 = (((RogueString__locate__String_OptionalInt32( ROGUE_ARG(THIS), st_0, (RogueOptionalInt32__create()) ))));
  if (ROGUE_COND(i_1.exists))
  {
    return (RogueString*)(((RogueString*)(RogueString__from__Int32_Int32( ROGUE_ARG(THIS), 0, ROGUE_ARG(((i_1.value) - (1))) ))));
  }
  else
  {
    return (RogueString*)(THIS);
  }
}

RogueString* RogueString__before_last__Character( RogueString* THIS, RogueCharacter ch_0 )
{
  ROGUE_GC_CHECK;
  RogueOptionalInt32 i_1 = (((RogueString__locate_last__Character_OptionalInt32( ROGUE_ARG(THIS), ch_0, (RogueOptionalInt32__create()) ))));
  if (ROGUE_COND(i_1.exists))
  {
    return (RogueString*)(((RogueString*)(RogueString__from__Int32_Int32( ROGUE_ARG(THIS), 0, ROGUE_ARG(((i_1.value) - (1))) ))));
  }
  else
  {
    return (RogueString*)(THIS);
  }
}

RogueLogical RogueString__begins_with__Character( RogueString* THIS, RogueCharacter ch_0 )
{
  ROGUE_GC_CHECK;
  return (RogueLogical)(((!!(THIS->character_count)) && (((RogueString_character_at(THIS,0)) == (ch_0)))));
}

RogueLogical RogueString__begins_with__String( RogueString* THIS, RogueString* other_0 )
{
  ROGUE_GC_CHECK;
  RogueInt32 other_count_1 = (other_0->character_count);
  return (RogueLogical)(((((THIS->character_count) >= (other_count_1))) && (((RogueString__contains_at__String_Int32( ROGUE_ARG(THIS), other_0, 0 ))))));
}

RogueString* RogueString__consolidated( RogueString* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(((RogueString*)(RogueStringConsolidationTable__get__String( ((RogueClassStringConsolidationTable*)ROGUE_SINGLETON(StringConsolidationTable)), ROGUE_ARG(THIS) ))));
}

RogueLogical RogueString__contains__Character( RogueString* THIS, RogueCharacter ch_0 )
{
  ROGUE_GC_CHECK;
  return (RogueLogical)(((RogueString__locate__Character_OptionalInt32( ROGUE_ARG(THIS), ch_0, (RogueOptionalInt32__create()) ))).exists);
}

RogueLogical RogueString__contains__String( RogueString* THIS, RogueString* substring_0 )
{
  ROGUE_GC_CHECK;
  return (RogueLogical)(((RogueString__locate__String_OptionalInt32( ROGUE_ARG(THIS), substring_0, (RogueOptionalInt32__create()) ))).exists);
}

RogueLogical RogueString__contains_at__String_Int32( RogueString* THIS, RogueString* substring_0, RogueInt32 at_index_1 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((at_index_1) < (0))))
  {
    return (RogueLogical)(false);
  }
  RogueInt32 offset = RogueString_set_cursor( THIS, at_index_1 );
  RogueInt32 other_count = substring_0->byte_count;
  if (offset + other_count > THIS->byte_count) return false;
  return (0 == memcmp(THIS->utf8 + offset, substring_0->utf8, other_count));

}

RogueLogical RogueString__ends_with__Character( RogueString* THIS, RogueCharacter ch_0 )
{
  ROGUE_GC_CHECK;
  return (RogueLogical)(((((THIS->character_count) > (0))) && (((RogueString_character_at(THIS,((THIS->character_count) - (1)))) == (ch_0)))));
}

RogueLogical RogueString__ends_with__String( RogueString* THIS, RogueString* other_0 )
{
  ROGUE_GC_CHECK;
  RogueInt32 other_count_1 = (other_0->character_count);
  return (RogueLogical)(((((((THIS->character_count) >= (other_count_1))) && (((other_count_1) > (0))))) && (((RogueString__contains_at__String_Int32( ROGUE_ARG(THIS), other_0, ROGUE_ARG(((THIS->character_count) - (other_count_1))) ))))));
}

RogueString* RogueString__from__Int32( RogueString* THIS, RogueInt32 i1_0 )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(((RogueString*)(RogueString__from__Int32_Int32( ROGUE_ARG(THIS), i1_0, ROGUE_ARG(((THIS->character_count) - (1))) ))));
}

RogueString* RogueString__from__Int32_Int32( RogueString* THIS, RogueInt32 i1_0, RogueInt32 i2_1 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((i1_0) < (0))))
  {
    i1_0 = ((RogueInt32)(0));
  }
  else if (ROGUE_COND(((i2_1) >= (THIS->character_count))))
  {
    i2_1 = ((RogueInt32)(((THIS->character_count) - (1))));
  }
  if (ROGUE_COND(((i1_0) > (i2_1))))
  {
    return (RogueString*)(Rogue_literal_strings[0]);
  }
  if (ROGUE_COND(((i1_0) == (i2_1))))
  {
    return (RogueString*)(((RogueString*)(RogueString__operatorPLUS__Character( ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], Rogue_literal_strings[0] ))), ROGUE_ARG(RogueString_character_at(THIS,i1_0)) ))));
  }
  RogueInt32 byte_i1 = RogueString_set_cursor( THIS, i1_0 );
  RogueInt32 byte_limit = RogueString_set_cursor( THIS, i2_1+1 );
  int new_count = (byte_limit - byte_i1);
  RogueString* result = RogueString_create_with_byte_count( new_count );
  memcpy( result->utf8, THIS->utf8+byte_i1, new_count );
  return RogueString_validate( result );

}

RogueString* RogueString__from_first__Character( RogueString* THIS, RogueCharacter ch_0 )
{
  ROGUE_GC_CHECK;
  RogueOptionalInt32 i_1 = (((RogueString__locate__Character_OptionalInt32( ROGUE_ARG(THIS), ch_0, (RogueOptionalInt32__create()) ))));
  if (ROGUE_COND(!(i_1.exists)))
  {
    return (RogueString*)(Rogue_literal_strings[0]);
  }
  return (RogueString*)(((RogueString*)(RogueString__from__Int32( ROGUE_ARG(THIS), ROGUE_ARG(i_1.value) ))));
}

RogueCharacter RogueString__last( RogueString* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueCharacter)(RogueString_character_at(THIS,((THIS->character_count) - (1))));
}

RogueString* RogueString__left_justified__Int32_Character( RogueString* THIS, RogueInt32 spaces_0, RogueCharacter fill_1 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((THIS->character_count) >= (spaces_0))))
  {
    return (RogueString*)(THIS);
  }
  ROGUE_DEF_LOCAL_REF(RogueStringBuilder*,buffer_2,(((RogueStringBuilder*)(RogueStringBuilder__init__Int32( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))), spaces_0 )))));
  RogueStringBuilder__print__String( buffer_2, ROGUE_ARG(THIS) );
  {
    RogueInt32 _auto_15_3 = (THIS->character_count);
    RogueInt32 _auto_16_4 = (spaces_0);
    for (;ROGUE_COND(((_auto_15_3) < (_auto_16_4)));++_auto_15_3)
    {
      ROGUE_GC_CHECK;
      RogueStringBuilder__print__Character_Logical( buffer_2, fill_1, true );
    }
  }
  return (RogueString*)(((RogueString*)(RogueStringBuilder__to_String( buffer_2 ))));
}

RogueString* RogueString__leftmost__Int32( RogueString* THIS, RogueInt32 n_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((n_0) >= (0))))
  {
    return (RogueString*)(((RogueString*)(RogueString__from__Int32_Int32( ROGUE_ARG(THIS), 0, ROGUE_ARG(((n_0) - (1))) ))));
  }
  else
  {
    return (RogueString*)(((RogueString*)(RogueString__from__Int32_Int32( ROGUE_ARG(THIS), 0, ROGUE_ARG(((((THIS->character_count) + (n_0))) - (1))) ))));
  }
}

RogueOptionalInt32 RogueString__locate__Character_OptionalInt32( RogueString* THIS, RogueCharacter ch_0, RogueOptionalInt32 optional_i1_1 )
{
  ROGUE_GC_CHECK;
  RogueInt32 i_2 = (0);
  RogueInt32 limit_3 = (THIS->character_count);
  if (ROGUE_COND(optional_i1_1.exists))
  {
    i_2 = ((RogueInt32)(optional_i1_1.value));
  }
  while (ROGUE_COND(((i_2) < (limit_3))))
  {
    ROGUE_GC_CHECK;
    if (ROGUE_COND(((RogueString_character_at(THIS,i_2)) == (ch_0))))
    {
      return (RogueOptionalInt32)(RogueOptionalInt32( i_2, true ));
    }
    ++i_2;
  }
  return (RogueOptionalInt32)((RogueOptionalInt32__create()));
}

RogueOptionalInt32 RogueString__locate__String_OptionalInt32( RogueString* THIS, RogueString* other_0, RogueOptionalInt32 optional_i1_1 )
{
  ROGUE_GC_CHECK;
  RogueInt32 other_count_2 = (other_0->character_count);
  if (ROGUE_COND(((other_count_2) == (1))))
  {
    return (RogueOptionalInt32)(((RogueString__locate__Character_OptionalInt32( ROGUE_ARG(THIS), ROGUE_ARG(RogueString_character_at(other_0,0)), optional_i1_1 ))));
  }
  RogueInt32 this_limit_3 = (((((THIS->character_count) - (other_count_2))) + (1)));
  if (ROGUE_COND(((((other_count_2) == (0))) || (((this_limit_3) <= (0))))))
  {
    return (RogueOptionalInt32)((RogueOptionalInt32__create()));
  }
  RogueInt32 i1_4 = 0;
  if (ROGUE_COND(optional_i1_1.exists))
  {
    i1_4 = ((RogueInt32)(((optional_i1_1.value) - (1))));
    if (ROGUE_COND(((i1_4) < (-1))))
    {
      i1_4 = ((RogueInt32)(-1));
    }
  }
  else
  {
    i1_4 = ((RogueInt32)(-1));
  }
  while (ROGUE_COND(((((RogueInt32)(++i1_4))) < (this_limit_3))))
  {
    ROGUE_GC_CHECK;
    if (ROGUE_COND(((RogueString__contains_at__String_Int32( ROGUE_ARG(THIS), other_0, i1_4 )))))
    {
      return (RogueOptionalInt32)(RogueOptionalInt32( i1_4, true ));
    }
  }
  return (RogueOptionalInt32)((RogueOptionalInt32__create()));
}

RogueOptionalInt32 RogueString__locate_last__Character_OptionalInt32( RogueString* THIS, RogueCharacter ch_0, RogueOptionalInt32 starting_index_1 )
{
  ROGUE_GC_CHECK;
  RogueInt32 i_2 = (((THIS->character_count) - (1)));
  if (ROGUE_COND(starting_index_1.exists))
  {
    i_2 = ((RogueInt32)(starting_index_1.value));
  }
  while (ROGUE_COND(((i_2) >= (0))))
  {
    ROGUE_GC_CHECK;
    if (ROGUE_COND(((RogueString_character_at(THIS,i_2)) == (ch_0))))
    {
      return (RogueOptionalInt32)(RogueOptionalInt32( i_2, true ));
    }
    --i_2;
  }
  return (RogueOptionalInt32)((RogueOptionalInt32__create()));
}

RogueString* RogueString__operatorPLUS__Character( RogueString* THIS, RogueCharacter value_0 )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__Character_Logical( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), ROGUE_ARG(THIS) )))), value_0, true )))) ))));
}

RogueString* RogueString__operatorPLUS__Int32( RogueString* THIS, RogueInt32 value_0 )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__Int32( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), ROGUE_ARG(THIS) )))), value_0 )))) ))));
}

RogueLogical RogueString__operatorEQUALSEQUALS__String( RogueString* THIS, RogueString* value_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((void*)value_0) == ((void*)NULL)))
  {
    return (RogueLogical)(false);
  }
  if (ROGUE_COND(((((((RogueString__hash_code( ROGUE_ARG(THIS) )))) != (((RogueString__hash_code( value_0 )))))) || (((THIS->character_count) != (value_0->character_count))))))
  {
    return (RogueLogical)(false);
  }
  return (RogueLogical)(((RogueLogical)((0==memcmp(THIS->utf8,value_0->utf8,THIS->byte_count)))));
}

RogueString* RogueString__operatorPLUS__Logical( RogueString* THIS, RogueLogical value_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(value_0))
  {
    return (RogueString*)((RogueString__operatorPLUS__String_String( ROGUE_ARG(THIS), Rogue_literal_strings[22] )));
  }
  else
  {
    return (RogueString*)((RogueString__operatorPLUS__String_String( ROGUE_ARG(THIS), Rogue_literal_strings[23] )));
  }
}

RogueString* RogueString__operatorPLUS__Object( RogueString* THIS, RogueObject* value_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(!!(value_0)))
  {
    return (RogueString*)((RogueString__operatorPLUS__String_String( ROGUE_ARG(THIS), ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, value_0 ))) )));
  }
  else
  {
    return (RogueString*)((RogueString__operatorPLUS__String_String( ROGUE_ARG(THIS), Rogue_literal_strings[1] )));
  }
}

RogueString* RogueString__operatorPLUS__String( RogueString* THIS, RogueString* value_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((void*)value_0) == ((void*)NULL)))
  {
    return (RogueString*)((RogueString__operatorPLUS__String_String( ROGUE_ARG(THIS), Rogue_literal_strings[1] )));
  }
  if (ROGUE_COND(((THIS->character_count) == (0))))
  {
    return (RogueString*)(value_0);
  }
  if (ROGUE_COND(((value_0->character_count) == (0))))
  {
    return (RogueString*)(THIS);
  }
  return (RogueString*)(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), ROGUE_ARG(THIS) )))), value_0 )))) ))));
}

RogueString* RogueString__pluralized__Int32( RogueString* THIS, RogueInt32 quantity_0 )
{
  ROGUE_GC_CHECK;
  ROGUE_DEF_LOCAL_REF(RogueString*,st_1,(((RogueString*)(RogueString__replacing__String_String( ROGUE_ARG(THIS), Rogue_literal_strings[40], ROGUE_ARG(((RogueString*)(RogueString__operatorPLUS__Int32( ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], Rogue_literal_strings[0] ))), quantity_0 )))) )))));
  if (ROGUE_COND(((RogueString__contains__Character( st_1, (RogueCharacter)'/' )))))
  {
    if (ROGUE_COND(((quantity_0) == (1))))
    {
      return (RogueString*)(((RogueString*)(RogueString__before_first__Character( st_1, (RogueCharacter)'/' ))));
    }
    else
    {
      return (RogueString*)(((RogueString*)(RogueString__after_last__Character( st_1, (RogueCharacter)'/' ))));
    }
  }
  else
  {
    RogueOptionalInt32 alt1_2 = (((RogueString__locate__Character_OptionalInt32( st_1, (RogueCharacter)'(', (RogueOptionalInt32__create()) ))));
    if (ROGUE_COND(alt1_2.exists))
    {
      RogueOptionalInt32 alt2_3 = (((RogueString__locate__Character_OptionalInt32( st_1, (RogueCharacter)')', RogueOptionalInt32( ((alt1_2.value) + (1)), true ) ))));
      if (ROGUE_COND(!(alt2_3.exists)))
      {
        return (RogueString*)(THIS);
      }
      if (ROGUE_COND(((quantity_0) == (1))))
      {
        return (RogueString*)((RogueString__operatorPLUS__String_String( ROGUE_ARG(((RogueString*)(RogueString__before__Int32( st_1, ROGUE_ARG(alt1_2.value) )))), ROGUE_ARG(((RogueString*)(RogueString__after__Int32( st_1, ROGUE_ARG(alt2_3.value) )))) )));
      }
      return (RogueString*)(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], ROGUE_ARG(((RogueString*)(RogueString__before__Int32( st_1, ROGUE_ARG(alt1_2.value) )))) ))) )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], ROGUE_ARG(((RogueString*)(RogueString__from__Int32_Int32( st_1, ROGUE_ARG(((alt1_2.value) + (1))), ROGUE_ARG(((alt2_3.value) - (1))) )))) ))) )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], ROGUE_ARG(((RogueString*)(RogueString__after__Int32( st_1, ROGUE_ARG(alt2_3.value) )))) ))) )))) ))));
    }
    else
    {
      if (ROGUE_COND(((quantity_0) == (1))))
      {
        return (RogueString*)(st_1);
      }
      RogueInt32 index_4 = (0);
      RogueInt32 i_5 = (st_1->character_count);
      while (ROGUE_COND(((i_5) > (0))))
      {
        ROGUE_GC_CHECK;
        --i_5;
        if (ROGUE_COND(((RogueCharacter__is_letter( ROGUE_ARG(RogueString_character_at(st_1,i_5)) )))))
        {
          index_4 = ((RogueInt32)(i_5));
          goto _auto_149;
        }
      }
      _auto_149:;
      if (ROGUE_COND(((RogueString_character_at(st_1,index_4)) == ((RogueCharacter)'s'))))
      {
        return (RogueString*)(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], ROGUE_ARG(((RogueString*)(RogueString__before__Int32( st_1, ROGUE_ARG(((index_4) + (1))) )))) ))) )))), Rogue_literal_strings[41] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], ROGUE_ARG(((RogueString*)(RogueString__after__Int32( st_1, index_4 )))) ))) )))) ))));
      }
      else
      {
        return (RogueString*)(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], ROGUE_ARG(((RogueString*)(RogueString__before__Int32( st_1, ROGUE_ARG(((index_4) + (1))) )))) ))) )))), Rogue_literal_strings[42] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], ROGUE_ARG(((RogueString*)(RogueString__after__Int32( st_1, index_4 )))) ))) )))) ))));
      }
    }
  }
}

RogueString* RogueString__replacing__String_String( RogueString* THIS, RogueString* look_for_0, RogueString* replace_with_1 )
{
  ROGUE_GC_CHECK;
  ROGUE_DEF_LOCAL_REF(RogueString*,st_2,(THIS));
  RogueOptionalInt32 i_3 = (((RogueString__locate__String_OptionalInt32( st_2, look_for_0, (RogueOptionalInt32__create()) ))));
  if (ROGUE_COND(!(i_3.exists)))
  {
    return (RogueString*)(st_2);
  }
  ROGUE_DEF_LOCAL_REF(RogueStringBuilder*,buffer_4,(((RogueStringBuilder*)(RogueStringBuilder__init__Int32( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))), ROGUE_ARG(((THIS->character_count) * (2))) )))));
  while (ROGUE_COND(i_3.exists))
  {
    ROGUE_GC_CHECK;
    RogueStringBuilder__print__String( buffer_4, ROGUE_ARG(((RogueString*)(RogueString__before__Int32( st_2, ROGUE_ARG(i_3.value) )))) );
    RogueStringBuilder__print__String( buffer_4, replace_with_1 );
    st_2 = ((RogueString*)(((RogueString*)(RogueString__from__Int32( st_2, ROGUE_ARG(((i_3.value) + (look_for_0->character_count))) )))));
    i_3 = ((RogueOptionalInt32)(((RogueString__locate__String_OptionalInt32( st_2, look_for_0, (RogueOptionalInt32__create()) )))));
  }
  RogueStringBuilder__print__String( buffer_4, st_2 );
  return (RogueString*)(((RogueString*)(RogueStringBuilder__to_String( buffer_4 ))));
}

RogueString* RogueString__rightmost__Int32( RogueString* THIS, RogueInt32 n_0 )
{
  ROGUE_GC_CHECK;
  RogueInt32 this_count_1 = (THIS->character_count);
  if (ROGUE_COND(((n_0) < (0))))
  {
    return (RogueString*)(((RogueString*)(RogueString__from__Int32_Int32( ROGUE_ARG(THIS), ROGUE_ARG((-(n_0))), ROGUE_ARG(((this_count_1) - (1))) ))));
  }
  else
  {
    return (RogueString*)(((RogueString*)(RogueString__from__Int32_Int32( ROGUE_ARG(THIS), ROGUE_ARG(((this_count_1) - (n_0))), ROGUE_ARG(((this_count_1) - (1))) ))));
  }
}

RogueString_List* RogueString__split__Character( RogueString* THIS, RogueCharacter separator_0 )
{
  ROGUE_GC_CHECK;
  ROGUE_DEF_LOCAL_REF(RogueString_List*,result_1,(((RogueString_List*)(RogueString_List__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueString_List*,ROGUE_CREATE_OBJECT(String_List))) )))));
  RogueInt32 i1_2 = (0);
  RogueOptionalInt32 i2_3 = (((RogueString__locate__Character_OptionalInt32( ROGUE_ARG(THIS), separator_0, RogueOptionalInt32( i1_2, true ) ))));
  while (ROGUE_COND(i2_3.exists))
  {
    ROGUE_GC_CHECK;
    RogueString_List__add__String( result_1, ROGUE_ARG(((RogueString*)(RogueString__from__Int32_Int32( ROGUE_ARG(THIS), i1_2, ROGUE_ARG(((i2_3.value) - (1))) )))) );
    i1_2 = ((RogueInt32)(((i2_3.value) + (1))));
    i2_3 = ((RogueOptionalInt32)(((RogueString__locate__Character_OptionalInt32( ROGUE_ARG(THIS), separator_0, RogueOptionalInt32( i1_2, true ) )))));
  }
  RogueString_List__add__String( result_1, ROGUE_ARG(((RogueString*)(RogueString__from__Int32( ROGUE_ARG(THIS), i1_2 )))) );
  return (RogueString_List*)(result_1);
}

RogueString* RogueString__times__Int32( RogueString* THIS, RogueInt32 n_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((n_0) <= (0))))
  {
    return (RogueString*)(Rogue_literal_strings[0]);
  }
  if (ROGUE_COND(((n_0) == (1))))
  {
    return (RogueString*)(THIS);
  }
  ROGUE_DEF_LOCAL_REF(RogueStringBuilder*,builder_1,(((RogueStringBuilder*)(RogueStringBuilder__init__Int32( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))), ROGUE_ARG(((THIS->character_count) * (n_0))) )))));
  {
    RogueInt32 _auto_21_2 = (1);
    RogueInt32 _auto_22_3 = (n_0);
    for (;ROGUE_COND(((_auto_21_2) <= (_auto_22_3)));++_auto_21_2)
    {
      ROGUE_GC_CHECK;
      RogueStringBuilder__print__String( builder_1, ROGUE_ARG(THIS) );
    }
  }
  return (RogueString*)(((RogueString*)(RogueStringBuilder__to_String( builder_1 ))));
}

RogueString* RogueString__to_lowercase( RogueString* THIS )
{
  ROGUE_GC_CHECK;
  RogueLogical has_uc_0 = (false);
  {
    ROGUE_DEF_LOCAL_REF(RogueString*,_auto_181_0,(THIS));
    RogueInt32 _auto_182_0 = (0);
    RogueInt32 _auto_183_0 = (((_auto_181_0->character_count) - (1)));
    for (;ROGUE_COND(((_auto_182_0) <= (_auto_183_0)));++_auto_182_0)
    {
      ROGUE_GC_CHECK;
      RogueCharacter ch_0 = (RogueString_character_at(_auto_181_0,_auto_182_0));
      if (ROGUE_COND(((((ch_0) >= ((RogueCharacter)'A'))) && (((ch_0) <= ((RogueCharacter)'Z'))))))
      {
        has_uc_0 = ((RogueLogical)(true));
        goto _auto_184;
      }
    }
  }
  _auto_184:;
  if (ROGUE_COND(!(has_uc_0)))
  {
    return (RogueString*)(THIS);
  }
  ROGUE_DEF_LOCAL_REF(RogueStringBuilder*,result_1,(((RogueStringBuilder*)(RogueStringBuilder__init__Int32( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))), ROGUE_ARG(THIS->character_count) )))));
  {
    ROGUE_DEF_LOCAL_REF(RogueString*,_auto_185_0,(THIS));
    RogueInt32 _auto_186_0 = (0);
    RogueInt32 _auto_187_0 = (((_auto_185_0->character_count) - (1)));
    for (;ROGUE_COND(((_auto_186_0) <= (_auto_187_0)));++_auto_186_0)
    {
      ROGUE_GC_CHECK;
      RogueCharacter ch_0 = (RogueString_character_at(_auto_185_0,_auto_186_0));
      if (ROGUE_COND(((((ch_0) >= ((RogueCharacter)'A'))) && (((ch_0) <= ((RogueCharacter)'Z'))))))
      {
        RogueStringBuilder__print__Character_Logical( result_1, ROGUE_ARG(((((ch_0) - ((RogueCharacter)'A'))) + ((RogueCharacter)'a'))), true );
      }
      else
      {
        RogueStringBuilder__print__Character_Logical( result_1, ch_0, true );
      }
    }
  }
  return (RogueString*)(((RogueString*)(RogueStringBuilder__to_String( result_1 ))));
}

RogueString* RogueString__trimmed( RogueString* THIS )
{
  ROGUE_GC_CHECK;
  RogueInt32 i1_0 = (0);
  RogueInt32 i2_1 = (((THIS->character_count) - (1)));
  while (ROGUE_COND(((i1_0) <= (i2_1))))
  {
    ROGUE_GC_CHECK;
    if (ROGUE_COND(((RogueString_character_at(THIS,i1_0)) <= ((RogueCharacter)' '))))
    {
      ++i1_0;
    }
    else if (ROGUE_COND(((RogueString_character_at(THIS,i2_1)) <= ((RogueCharacter)' '))))
    {
      --i2_1;
    }
    else
    {
      goto _auto_195;
    }
  }
  _auto_195:;
  if (ROGUE_COND(((i1_0) > (i2_1))))
  {
    return (RogueString*)(Rogue_literal_strings[0]);
  }
  if (ROGUE_COND(((((i1_0) == (0))) && (((i2_1) == (((THIS->character_count) - (1))))))))
  {
    return (RogueString*)(THIS);
  }
  return (RogueString*)(((RogueString*)(RogueString__from__Int32_Int32( ROGUE_ARG(THIS), i1_0, i2_1 ))));
}

RogueString_List* RogueString__word_wrap__Int32_String( RogueString* THIS, RogueInt32 width_0, RogueString* allow_break_after_1 )
{
  ROGUE_GC_CHECK;
  return (RogueString_List*)(((RogueString_List*)(RogueString__split__Character( ROGUE_ARG(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueString__word_wrap__Int32_StringBuilder_String( ROGUE_ARG(THIS), width_0, ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), allow_break_after_1 )))) )))), (RogueCharacter)10 ))));
}

RogueStringBuilder* RogueString__word_wrap__Int32_StringBuilder_String( RogueString* THIS, RogueInt32 width_0, RogueStringBuilder* buffer_1, RogueString* allow_break_after_2 )
{
  ROGUE_GC_CHECK;
  RogueInt32 i1_3 = 0;
  RogueInt32 i2_4 = 0;
  RogueInt32 len_5 = (THIS->character_count);
  if (ROGUE_COND(((len_5) == (0))))
  {
    return (RogueStringBuilder*)(buffer_1);
  }
  RogueInt32 w_6 = (width_0);
  RogueInt32 initial_indent_7 = (0);
  {
    ROGUE_DEF_LOCAL_REF(RogueString*,_auto_196_0,(THIS));
    RogueInt32 _auto_197_0 = (0);
    RogueInt32 _auto_198_0 = (((_auto_196_0->character_count) - (1)));
    for (;ROGUE_COND(((_auto_197_0) <= (_auto_198_0)));++_auto_197_0)
    {
      ROGUE_GC_CHECK;
      RogueCharacter ch_0 = (RogueString_character_at(_auto_196_0,_auto_197_0));
      if (ROGUE_COND(((ch_0) != ((RogueCharacter)' '))))
      {
        goto _auto_199;
      }
      ++initial_indent_7;
      --w_6;
      ++i1_3;
    }
  }
  _auto_199:;
  if (ROGUE_COND(((w_6) <= (0))))
  {
    w_6 = ((RogueInt32)(width_0));
    initial_indent_7 = ((RogueInt32)(0));
    RogueStringBuilder__println( buffer_1 );
  }
  else
  {
    {
      RogueInt32 _auto_24_8 = (1);
      RogueInt32 _auto_25_9 = (((width_0) - (w_6)));
      for (;ROGUE_COND(((_auto_24_8) <= (_auto_25_9)));++_auto_24_8)
      {
        ROGUE_GC_CHECK;
        RogueStringBuilder__print__Character_Logical( buffer_1, (RogueCharacter)' ', true );
      }
    }
  }
  RogueLogical needs_newline_10 = (false);
  while (ROGUE_COND(((i2_4) < (len_5))))
  {
    ROGUE_GC_CHECK;
    while (ROGUE_COND(((((((((i2_4) - (i1_3))) < (w_6))) && (((i2_4) < (len_5))))) && (((RogueString_character_at(THIS,i2_4)) != ((RogueCharacter)10))))))
    {
      ROGUE_GC_CHECK;
      ++i2_4;
    }
    if (ROGUE_COND(((((i2_4) - (i1_3))) == (w_6))))
    {
      if (ROGUE_COND(((i2_4) >= (len_5))))
      {
        i2_4 = ((RogueInt32)(len_5));
      }
      else if (ROGUE_COND(((RogueString_character_at(THIS,i2_4)) != ((RogueCharacter)10))))
      {
        while (ROGUE_COND(((((RogueString_character_at(THIS,i2_4)) != ((RogueCharacter)' '))) && (((i2_4) > (i1_3))))))
        {
          ROGUE_GC_CHECK;
          --i2_4;
        }
        if (ROGUE_COND(((i2_4) == (i1_3))))
        {
          i2_4 = ((RogueInt32)(((i1_3) + (w_6))));
          if (ROGUE_COND(!!(allow_break_after_2)))
          {
            while (ROGUE_COND(((((((i2_4) > (i1_3))) && (!(((RogueString__contains__Character( allow_break_after_2, ROGUE_ARG(RogueString_character_at(THIS,((i2_4) - (1)))) ))))))) && (((i2_4) > (i1_3))))))
            {
              ROGUE_GC_CHECK;
              --i2_4;
            }
            if (ROGUE_COND(((i2_4) == (i1_3))))
            {
              i2_4 = ((RogueInt32)(((i1_3) + (w_6))));
            }
          }
        }
      }
    }
    if (ROGUE_COND(needs_newline_10))
    {
      RogueStringBuilder__println( buffer_1 );
      if (ROGUE_COND(!!(initial_indent_7)))
      {
        {
          RogueInt32 _auto_26_11 = (1);
          RogueInt32 _auto_27_12 = (initial_indent_7);
          for (;ROGUE_COND(((_auto_26_11) <= (_auto_27_12)));++_auto_26_11)
          {
            ROGUE_GC_CHECK;
            RogueStringBuilder__print__Character_Logical( buffer_1, (RogueCharacter)' ', true );
          }
        }
      }
    }
    {
      RogueInt32 i_13 = (i1_3);
      RogueInt32 _auto_28_14 = (((i2_4) - (1)));
      for (;ROGUE_COND(((i_13) <= (_auto_28_14)));++i_13)
      {
        ROGUE_GC_CHECK;
        RogueStringBuilder__print__Character_Logical( buffer_1, ROGUE_ARG(RogueString_character_at(THIS,i_13)), true );
      }
    }
    needs_newline_10 = ((RogueLogical)(true));
    if (ROGUE_COND(((i2_4) == (len_5))))
    {
      return (RogueStringBuilder*)(buffer_1);
    }
    else
    {
      switch (RogueString_character_at(THIS,i2_4))
      {
        case (RogueCharacter)' ':
        {
          while (ROGUE_COND(((((i2_4) < (len_5))) && (((RogueString_character_at(THIS,i2_4)) == ((RogueCharacter)' '))))))
          {
            ROGUE_GC_CHECK;
            ++i2_4;
          }
          if (ROGUE_COND(((((i2_4) < (len_5))) && (((RogueString_character_at(THIS,i2_4)) == ((RogueCharacter)10))))))
          {
            ++i2_4;
          }
          i1_3 = ((RogueInt32)(i2_4));
          break;
        }
        case (RogueCharacter)10:
        {
          ++i2_4;
          w_6 = ((RogueInt32)(width_0));
          initial_indent_7 = ((RogueInt32)(0));
          {
            RogueInt32 i_15 = (i2_4);
            RogueInt32 _auto_29_16 = (len_5);
            for (;ROGUE_COND(((i_15) < (_auto_29_16)));++i_15)
            {
              ROGUE_GC_CHECK;
              if (ROGUE_COND(((RogueString_character_at(THIS,i_15)) != ((RogueCharacter)' '))))
              {
                goto _auto_200;
              }
              ++initial_indent_7;
              --w_6;
              ++i2_4;
            }
          }
          _auto_200:;
          if (ROGUE_COND(((w_6) <= (0))))
          {
            w_6 = ((RogueInt32)(width_0));
            initial_indent_7 = ((RogueInt32)(0));
          }
          else
          {
            {
              RogueInt32 _auto_30_17 = (1);
              RogueInt32 _auto_31_18 = (((width_0) - (w_6)));
              for (;ROGUE_COND(((_auto_30_17) <= (_auto_31_18)));++_auto_30_17)
              {
                ROGUE_GC_CHECK;
                RogueStringBuilder__print__Character_Logical( buffer_1, (RogueCharacter)' ', true );
              }
            }
          }
          break;
        }
      }
      i1_3 = ((RogueInt32)(i2_4));
    }
  }
  return (RogueStringBuilder*)(buffer_1);
}

RogueClass_Function_TableEntry_String_Value__TableEntry_String_Value__RETURNSLogical_* Rogue_Function_TableEntry_String_Value__TableEntry_String_Value__RETURNSLogical___init_object( RogueClass_Function_TableEntry_String_Value__TableEntry_String_Value__RETURNSLogical_* THIS )
{
  ROGUE_GC_CHECK;
  RogueObject__init_object( ROGUE_ARG(((RogueObject*)THIS)) );
  return (RogueClass_Function_TableEntry_String_Value__TableEntry_String_Value__RETURNSLogical_*)(THIS);
}

RogueString* Rogue_Function_TableEntry_String_Value__TableEntry_String_Value__RETURNSLogical___type_name( RogueClass_Function_TableEntry_String_Value__TableEntry_String_Value__RETURNSLogical_* THIS )
{
  return (RogueString*)(Rogue_literal_strings[159]);
}

RogueLogical Rogue_Function_TableEntry_String_Value__TableEntry_String_Value__RETURNSLogical___call__TableEntry_String_Value__TableEntry_String_Value_( RogueClass_Function_TableEntry_String_Value__TableEntry_String_Value__RETURNSLogical_* THIS, RogueClassTableEntry_String_Value_* param1_0, RogueClassTableEntry_String_Value_* param2_1 )
{
  return (RogueLogical)(false);
}

RogueStringBuilder* RogueStringBuilder__init_object( RogueStringBuilder* THIS )
{
  ROGUE_GC_CHECK;
  RogueObject__init_object( ROGUE_ARG(((RogueObject*)THIS)) );
  THIS->at_newline = true;
  return (RogueStringBuilder*)(THIS);
}

RogueStringBuilder* RogueStringBuilder__init( RogueStringBuilder* THIS )
{
  ROGUE_GC_CHECK;
  RogueStringBuilder__init__Int32( ROGUE_ARG(THIS), 40 );
  return (RogueStringBuilder*)(THIS);
}

RogueString* RogueStringBuilder__to_String( RogueStringBuilder* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(RogueString_create_from_utf8( (char*)THIS->utf8->data->as_bytes, THIS->utf8->count ));
}

RogueString* RogueStringBuilder__type_name( RogueStringBuilder* THIS )
{
  return (RogueString*)(Rogue_literal_strings[137]);
}

RogueStringBuilder* RogueStringBuilder__init__Int32( RogueStringBuilder* THIS, RogueInt32 initial_capacity_0 )
{
  ROGUE_GC_CHECK;
  THIS->utf8 = ((RogueByte_List*)(RogueByte_List__init__Int32( ROGUE_ARG(ROGUE_CREATE_REF(RogueByte_List*,ROGUE_CREATE_OBJECT(Byte_List))), initial_capacity_0 )));
  return (RogueStringBuilder*)(THIS);
}

RogueStringBuilder* RogueStringBuilder__clear( RogueStringBuilder* THIS )
{
  ROGUE_GC_CHECK;
  RogueByte_List__clear( ROGUE_ARG(THIS->utf8) );
  THIS->count = 0;
  THIS->at_newline = true;
  THIS->cursor_index = 0;
  THIS->cursor_offset = 0;
  return (RogueStringBuilder*)(THIS);
}

RogueCharacter RogueStringBuilder__get__Int32( RogueStringBuilder* THIS, RogueInt32 index_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((index_0) == (0))))
  {
    THIS->cursor_offset = 0;
    THIS->cursor_index = 0;
  }
  else if (ROGUE_COND(((index_0) == (((THIS->count) - (1))))))
  {
    THIS->cursor_offset = THIS->utf8->count;
    THIS->cursor_index = THIS->count;
  }
  while (ROGUE_COND(((THIS->cursor_index) < (index_0))))
  {
    ROGUE_GC_CHECK;
    ++THIS->cursor_offset;
    while (ROGUE_COND(((((((RogueInt32)(THIS->utf8->data->as_bytes[THIS->cursor_offset]))) & (192))) == (128))))
    {
      ROGUE_GC_CHECK;
      ++THIS->cursor_offset;
    }
    ++THIS->cursor_index;
  }
  while (ROGUE_COND(((THIS->cursor_index) > (index_0))))
  {
    ROGUE_GC_CHECK;
    --THIS->cursor_offset;
    while (ROGUE_COND(((((((RogueInt32)(THIS->utf8->data->as_bytes[THIS->cursor_offset]))) & (192))) == (128))))
    {
      ROGUE_GC_CHECK;
      --THIS->cursor_offset;
    }
    --THIS->cursor_index;
  }
  RogueByte ch_1 = (THIS->utf8->data->as_bytes[THIS->cursor_offset]);
  if (ROGUE_COND(!!(((((RogueInt32)(ch_1))) & (128)))))
  {
    if (ROGUE_COND(!!(((((RogueInt32)(ch_1))) & (32)))))
    {
      if (ROGUE_COND(!!(((((RogueInt32)(ch_1))) & (16)))))
      {
        return (RogueCharacter)(((RogueCharacter)(((((((((((((RogueInt32)(ch_1))) & (7))) << (18))) | (((((((RogueInt32)(THIS->utf8->data->as_bytes[((THIS->cursor_offset) + (1))]))) & (63))) << (12))))) | (((((((RogueInt32)(THIS->utf8->data->as_bytes[((THIS->cursor_offset) + (2))]))) & (63))) << (6))))) | (((((RogueInt32)(THIS->utf8->data->as_bytes[((THIS->cursor_offset) + (3))]))) & (63)))))));
      }
      else
      {
        return (RogueCharacter)(((RogueCharacter)(((((((((((RogueInt32)(ch_1))) & (15))) << (12))) | (((((((RogueInt32)(THIS->utf8->data->as_bytes[((THIS->cursor_offset) + (1))]))) & (63))) << (6))))) | (((((RogueInt32)(THIS->utf8->data->as_bytes[((THIS->cursor_offset) + (2))]))) & (63)))))));
      }
    }
    else
    {
      return (RogueCharacter)(((RogueCharacter)(((((((((RogueInt32)(ch_1))) & (31))) << (6))) | (((((RogueInt32)(THIS->utf8->data->as_bytes[((THIS->cursor_offset) + (1))]))) & (63)))))));
    }
  }
  else
  {
    return (RogueCharacter)(((RogueCharacter)(ch_1)));
  }
}

RogueOptionalInt32 RogueStringBuilder__locate__Character( RogueStringBuilder* THIS, RogueCharacter ch_0 )
{
  ROGUE_GC_CHECK;
  {
    ROGUE_DEF_LOCAL_REF(RogueStringBuilder*,_auto_303_0,(THIS));
    RogueInt32 i_0 = (0);
    RogueInt32 _auto_304_0 = (((_auto_303_0->count) - (1)));
    for (;ROGUE_COND(((i_0) <= (_auto_304_0)));++i_0)
    {
      ROGUE_GC_CHECK;
      if (ROGUE_COND(((((RogueStringBuilder__get__Int32( ROGUE_ARG(THIS), i_0 )))) == (ch_0))))
      {
        return (RogueOptionalInt32)(RogueOptionalInt32( i_0, true ));
      }
    }
  }
  return (RogueOptionalInt32)((RogueOptionalInt32__create()));
}

RogueLogical RogueStringBuilder__needs_indent( RogueStringBuilder* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueLogical)(((THIS->at_newline) && (((THIS->indent) > (0)))));
}

RogueStringBuilder* RogueStringBuilder__print__Character_Logical( RogueStringBuilder* THIS, RogueCharacter value_0, RogueLogical formatted_1 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(formatted_1))
  {
    if (ROGUE_COND(((value_0) == ((RogueCharacter)10))))
    {
      THIS->at_newline = true;
    }
    else if (ROGUE_COND(((RogueStringBuilder__needs_indent( ROGUE_ARG(THIS) )))))
    {
      RogueStringBuilder__print_indent( ROGUE_ARG(THIS) );
    }
  }
  ++THIS->count;
  if (ROGUE_COND(((((RogueInt32)(value_0))) < (0))))
  {
    RogueByte_List__add__Byte( ROGUE_ARG(THIS->utf8), 0 );
  }
  else if (ROGUE_COND(((value_0) <= ((RogueCharacter__create__Int32( 127 ))))))
  {
    RogueByte_List__add__Byte( ROGUE_ARG(THIS->utf8), ROGUE_ARG(((RogueByte)(value_0))) );
  }
  else if (ROGUE_COND(((value_0) <= ((RogueCharacter__create__Int32( 2047 ))))))
  {
    RogueByte_List__add__Byte( ROGUE_ARG(((RogueByte_List*)(RogueByte_List__add__Byte( ROGUE_ARG(THIS->utf8), ROGUE_ARG(((RogueByte)(((192) | (((((RogueInt32)(value_0))) >> (6))))))) )))), ROGUE_ARG(((RogueByte)(((128) | (((((RogueInt32)(value_0))) & (63))))))) );
  }
  else if (ROGUE_COND(((value_0) <= ((RogueCharacter__create__Int32( 65535 ))))))
  {
    RogueByte_List__add__Byte( ROGUE_ARG(((RogueByte_List*)(RogueByte_List__add__Byte( ROGUE_ARG(((RogueByte_List*)(RogueByte_List__add__Byte( ROGUE_ARG(THIS->utf8), ROGUE_ARG(((RogueByte)(((224) | (((((RogueInt32)(value_0))) >> (12))))))) )))), ROGUE_ARG(((RogueByte)(((128) | (((((((RogueInt32)(value_0))) >> (6))) & (63))))))) )))), ROGUE_ARG(((RogueByte)(((128) | (((((RogueInt32)(value_0))) & (63))))))) );
  }
  else if (ROGUE_COND(((value_0) <= ((RogueCharacter__create__Int32( 1114111 ))))))
  {
    RogueByte_List__add__Byte( ROGUE_ARG(((RogueByte_List*)(RogueByte_List__add__Byte( ROGUE_ARG(THIS->utf8), ROGUE_ARG(((RogueByte)(((240) | (((((RogueInt32)(value_0))) >> (18))))))) )))), ROGUE_ARG(((RogueByte)(((128) | (((((((RogueInt32)(value_0))) >> (12))) & (63))))))) );
    RogueByte_List__add__Byte( ROGUE_ARG(((RogueByte_List*)(RogueByte_List__add__Byte( ROGUE_ARG(THIS->utf8), ROGUE_ARG(((RogueByte)(((128) | (((((((RogueInt32)(value_0))) >> (6))) & (63))))))) )))), ROGUE_ARG(((RogueByte)(((128) | (((((RogueInt32)(value_0))) & (63))))))) );
  }
  else
  {
    RogueByte_List__add__Byte( ROGUE_ARG(THIS->utf8), 63 );
  }
  return (RogueStringBuilder*)(THIS);
}

RogueStringBuilder* RogueStringBuilder__print__Int32( RogueStringBuilder* THIS, RogueInt32 value_0 )
{
  ROGUE_GC_CHECK;
  return (RogueStringBuilder*)(((RogueStringBuilder*)(RogueStringBuilder__print__Int64( ROGUE_ARG(THIS), ROGUE_ARG(((RogueInt64)(value_0))) ))));
}

RogueStringBuilder* RogueStringBuilder__print__Logical( RogueStringBuilder* THIS, RogueLogical value_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(value_0))
  {
    return (RogueStringBuilder*)(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(THIS), Rogue_literal_strings[22] ))));
  }
  else
  {
    return (RogueStringBuilder*)(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(THIS), Rogue_literal_strings[23] ))));
  }
}

RogueStringBuilder* RogueStringBuilder__print__Int64( RogueStringBuilder* THIS, RogueInt64 value_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((value_0) == (((1LL) << (63LL))))))
  {
    return (RogueStringBuilder*)(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(THIS), Rogue_literal_strings[35] ))));
  }
  else if (ROGUE_COND(((value_0) < (0LL))))
  {
    RogueStringBuilder__print__Character_Logical( ROGUE_ARG(THIS), (RogueCharacter)'-', true );
    return (RogueStringBuilder*)(((RogueStringBuilder*)(RogueStringBuilder__print__Int64( ROGUE_ARG(THIS), ROGUE_ARG((-(value_0))) ))));
  }
  else if (ROGUE_COND(((value_0) >= (10LL))))
  {
    RogueStringBuilder__print__Int64( ROGUE_ARG(THIS), ROGUE_ARG(((value_0) / (10LL))) );
    return (RogueStringBuilder*)(((RogueStringBuilder*)(RogueStringBuilder__print__Character_Logical( ROGUE_ARG(THIS), ROGUE_ARG(((RogueCharacter)(((48LL) + ((RogueMath__mod__Int64_Int64( value_0, 10LL ))))))), true ))));
  }
  else
  {
    return (RogueStringBuilder*)(((RogueStringBuilder*)(RogueStringBuilder__print__Character_Logical( ROGUE_ARG(THIS), ROGUE_ARG(((RogueCharacter)(((48LL) + (value_0))))), true ))));
  }
}

RogueStringBuilder* RogueStringBuilder__print__Object( RogueStringBuilder* THIS, RogueObject* value_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(!!(value_0)))
  {
    return (RogueStringBuilder*)(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(THIS), ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, value_0 ))) ))));
  }
  return (RogueStringBuilder*)(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(THIS), Rogue_literal_strings[1] ))));
}

RogueStringBuilder* RogueStringBuilder__print__Real64( RogueStringBuilder* THIS, RogueReal64 value_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((value_0) == (0.0))))
  {
    RogueStringBuilder__print__String( ROGUE_ARG(THIS), Rogue_literal_strings[130] );
    return (RogueStringBuilder*)(THIS);
  }
  else if (ROGUE_COND(((RogueReal64__is_infinite( value_0 )))))
  {
    if (ROGUE_COND(((value_0) < (0.0))))
    {
      RogueStringBuilder__print__String( ROGUE_ARG(THIS), Rogue_literal_strings[131] );
    }
    else
    {
      RogueStringBuilder__print__String( ROGUE_ARG(THIS), Rogue_literal_strings[132] );
    }
    return (RogueStringBuilder*)(THIS);
  }
  else if (ROGUE_COND(((RogueReal64__is_not_a_number( value_0 )))))
  {
    RogueStringBuilder__print__String( ROGUE_ARG(THIS), Rogue_literal_strings[133] );
    return (RogueStringBuilder*)(THIS);
  }
  if (ROGUE_COND(((value_0) < (0.0))))
  {
    RogueStringBuilder__print__Character_Logical( ROGUE_ARG(THIS), (RogueCharacter)'-', true );
    value_0 = ((RogueReal64)((-(value_0))));
  }
  if (ROGUE_COND(((value_0) >= (1.0e15))))
  {
    RogueInt32 pow10_1 = (0);
    while (ROGUE_COND(((value_0) >= (10.0))))
    {
      ROGUE_GC_CHECK;
      value_0 /= 10.0;
      ++pow10_1;
    }
    return (RogueStringBuilder*)(((RogueStringBuilder*)(RogueStringBuilder__print__Int32( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__Character_Logical( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__Real64( ROGUE_ARG(THIS), value_0 )))), (RogueCharacter)'e', true )))), pow10_1 ))));
  }
  if (ROGUE_COND(((value_0) < (0.00001))))
  {
    RogueInt32 pow10_2 = (0);
    while (ROGUE_COND(((value_0) < (0.1))))
    {
      ROGUE_GC_CHECK;
      value_0 *= 10.0;
      --pow10_2;
    }
    return (RogueStringBuilder*)(((RogueStringBuilder*)(RogueStringBuilder__print__Int32( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__Character_Logical( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__Real64( ROGUE_ARG(THIS), value_0 )))), (RogueCharacter)'e', true )))), pow10_2 ))));
  }
  {
    RogueInt32 decimal_count_3 = (1);
    RogueInt32 _auto_32_4 = (18);
    for (;ROGUE_COND(((decimal_count_3) <= (_auto_32_4)));++decimal_count_3)
    {
      ROGUE_GC_CHECK;
      RogueStringBuilder__print_to_work_bytes__Real64_Int32( ROGUE_ARG(THIS), value_0, decimal_count_3 );
      if (ROGUE_COND(((((RogueStringBuilder__scan_work_bytes( ROGUE_ARG(THIS) )))) == (value_0))))
      {
        goto _auto_310;
      }
    }
  }
  _auto_310:;
  RogueStringBuilder__print_work_bytes( ROGUE_ARG(THIS) );
  return (RogueStringBuilder*)(THIS);
}

RogueStringBuilder* RogueStringBuilder__print__Real64_Int32( RogueStringBuilder* THIS, RogueReal64 value_0, RogueInt32 decimal_places_1 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((RogueReal64__is_infinite( value_0 )))))
  {
    if (ROGUE_COND(((value_0) < (0.0))))
    {
      RogueStringBuilder__print__String( ROGUE_ARG(THIS), Rogue_literal_strings[131] );
    }
    else
    {
      RogueStringBuilder__print__String( ROGUE_ARG(THIS), Rogue_literal_strings[132] );
    }
    return (RogueStringBuilder*)(THIS);
  }
  else if (ROGUE_COND(((RogueReal64__is_not_a_number( value_0 )))))
  {
    RogueStringBuilder__print__String( ROGUE_ARG(THIS), Rogue_literal_strings[133] );
    return (RogueStringBuilder*)(THIS);
  }
  if (ROGUE_COND(((value_0) < (0.0))))
  {
    RogueStringBuilder__print__Character_Logical( ROGUE_ARG(THIS), (RogueCharacter)'-', true );
    value_0 = ((RogueReal64)((-(value_0))));
  }
  RogueStringBuilder__print_to_work_bytes__Real64_Int32( ROGUE_ARG(THIS), value_0, decimal_places_1 );
  RogueStringBuilder__print_work_bytes( ROGUE_ARG(THIS) );
  return (RogueStringBuilder*)(THIS);
}

RogueStringBuilder* RogueStringBuilder__print__String( RogueStringBuilder* THIS, RogueString* value_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(!!(value_0)))
  {
    if (ROGUE_COND(!!(THIS->indent)))
    {
      {
        ROGUE_DEF_LOCAL_REF(RogueString*,_auto_311_0,(value_0));
        RogueInt32 _auto_312_0 = (0);
        RogueInt32 _auto_313_0 = (((_auto_311_0->character_count) - (1)));
        for (;ROGUE_COND(((_auto_312_0) <= (_auto_313_0)));++_auto_312_0)
        {
          ROGUE_GC_CHECK;
          RogueCharacter ch_0 = (RogueString_character_at(_auto_311_0,_auto_312_0));
          RogueStringBuilder__print__Character_Logical( ROGUE_ARG(THIS), ch_0, true );
        }
      }
    }
    else
    {
      {
        RogueInt32 i_1 = (0);
        RogueInt32 _auto_33_2 = (value_0->byte_count);
        for (;ROGUE_COND(((i_1) < (_auto_33_2)));++i_1)
        {
          ROGUE_GC_CHECK;
          RogueByte_List__add__Byte( ROGUE_ARG(THIS->utf8), ROGUE_ARG(value_0->utf8[ i_1 ]) );
        }
      }
      THIS->count += value_0->character_count;
      if (ROGUE_COND(((!!(value_0->character_count)) && (((((RogueString__last( value_0 )))) == ((RogueCharacter)10))))))
      {
        THIS->at_newline = true;
      }
    }
    return (RogueStringBuilder*)(THIS);
  }
  else
  {
    return (RogueStringBuilder*)(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(THIS), Rogue_literal_strings[1] ))));
  }
}

void RogueStringBuilder__print_indent( RogueStringBuilder* THIS )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((!(((RogueStringBuilder__needs_indent( ROGUE_ARG(THIS) ))))) || (((THIS->indent) == (0))))))
  {
    return;
  }
  {
    RogueInt32 i_0 = (1);
    RogueInt32 _auto_34_1 = (THIS->indent);
    for (;ROGUE_COND(((i_0) <= (_auto_34_1)));++i_0)
    {
      ROGUE_GC_CHECK;
      RogueByte_List__add__Byte( ROGUE_ARG(THIS->utf8), 32 );
    }
  }
  THIS->count += THIS->indent;
  THIS->at_newline = false;
}

RogueStringBuilder* RogueStringBuilder__print_to_work_bytes__Real64_Int32( RogueStringBuilder* THIS, RogueReal64 value_0, RogueInt32 decimal_places_1 )
{
  ROGUE_GC_CHECK;
  RogueByte_List__clear( ROGUE_ARG(RogueStringBuilder_work_bytes) );
  RogueReal64 whole_2 = (floor((double)value_0));
  value_0 -= whole_2;
  while (ROGUE_COND(((whole_2) >= (10.0))))
  {
    ROGUE_GC_CHECK;
    RogueByte_List__add__Byte( ROGUE_ARG(RogueStringBuilder_work_bytes), ROGUE_ARG(((RogueByte)(((RogueCharacter)(((48) + (((RogueInt32)((RogueMath__mod__Real64_Real64( whole_2, 10.0 ))))))))))) );
    whole_2 /= 10.0;
  }
  RogueByte_List__add__Byte( ROGUE_ARG(RogueStringBuilder_work_bytes), ROGUE_ARG(((RogueByte)(((RogueCharacter)(((48) + (((RogueInt32)((RogueMath__mod__Real64_Real64( whole_2, 10.0 ))))))))))) );
  RogueByte_List__reverse( ROGUE_ARG(RogueStringBuilder_work_bytes) );
  if (ROGUE_COND(((decimal_places_1) != (0))))
  {
    RogueByte_List__add__Byte( ROGUE_ARG(RogueStringBuilder_work_bytes), 46 );
    {
      RogueInt32 _auto_35_4 = (1);
      RogueInt32 _auto_36_5 = (decimal_places_1);
      for (;ROGUE_COND(((_auto_35_4) <= (_auto_36_5)));++_auto_35_4)
      {
        ROGUE_GC_CHECK;
        value_0 *= 10.0;
        RogueInt32 digit_3 = (((RogueInt32)(floor((double)value_0))));
        value_0 -= ((RogueReal64)(digit_3));
        RogueByte_List__add__Byte( ROGUE_ARG(RogueStringBuilder_work_bytes), ROGUE_ARG(((RogueByte)(((RogueCharacter)(((48) + (digit_3))))))) );
      }
    }
  }
  if (ROGUE_COND(((value_0) >= (0.5))))
  {
    RogueByte_List__add__Byte( ROGUE_ARG(RogueStringBuilder_work_bytes), 53 );
    RogueStringBuilder__round_off_work_bytes( ROGUE_ARG(THIS) );
  }
  return (RogueStringBuilder*)(THIS);
}

void RogueStringBuilder__print_work_bytes( RogueStringBuilder* THIS )
{
  ROGUE_GC_CHECK;
  {
    ROGUE_DEF_LOCAL_REF(RogueByte_List*,_auto_314_0,(RogueStringBuilder_work_bytes));
    RogueInt32 _auto_315_0 = (0);
    RogueInt32 _auto_316_0 = (((_auto_314_0->count) - (1)));
    for (;ROGUE_COND(((_auto_315_0) <= (_auto_316_0)));++_auto_315_0)
    {
      ROGUE_GC_CHECK;
      RogueByte digit_0 = (_auto_314_0->data->as_bytes[_auto_315_0]);
      RogueStringBuilder__print__Character_Logical( ROGUE_ARG(THIS), ROGUE_ARG(((RogueCharacter)(digit_0))), true );
    }
  }
}

RogueStringBuilder* RogueStringBuilder__println( RogueStringBuilder* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueStringBuilder*)(((RogueStringBuilder*)(RogueStringBuilder__print__Character_Logical( ROGUE_ARG(THIS), (RogueCharacter)10, true ))));
}

RogueStringBuilder* RogueStringBuilder__println__String( RogueStringBuilder* THIS, RogueString* value_0 )
{
  ROGUE_GC_CHECK;
  return (RogueStringBuilder*)(((RogueStringBuilder*)(RogueStringBuilder__print__Character_Logical( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(THIS), value_0 )))), (RogueCharacter)10, true ))));
}

RogueStringBuilder* RogueStringBuilder__reserve__Int32( RogueStringBuilder* THIS, RogueInt32 additional_bytes_0 )
{
  ROGUE_GC_CHECK;
  RogueByte_List__reserve__Int32( ROGUE_ARG(THIS->utf8), additional_bytes_0 );
  return (RogueStringBuilder*)(THIS);
}

void RogueStringBuilder__round_off_work_bytes( RogueStringBuilder* THIS )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((((RogueCharacter)(((RogueByte_List__remove_last( ROGUE_ARG(RogueStringBuilder_work_bytes) )))))) >= ((RogueCharacter)'5'))))
  {
    RogueInt32 i_0 = (((RogueStringBuilder_work_bytes->count) - (1)));
    while (ROGUE_COND(((i_0) >= (0))))
    {
      ROGUE_GC_CHECK;
      if (ROGUE_COND(((((RogueCharacter)(RogueStringBuilder_work_bytes->data->as_bytes[i_0]))) != ((RogueCharacter)'.'))))
      {
        RogueStringBuilder_work_bytes->data->as_bytes[i_0] = (((RogueByte)(((((RogueInt32)(RogueStringBuilder_work_bytes->data->as_bytes[i_0]))) + (1)))));
        if (ROGUE_COND(((((RogueInt32)(RogueStringBuilder_work_bytes->data->as_bytes[i_0]))) == (58))))
        {
          RogueStringBuilder_work_bytes->data->as_bytes[i_0] = (48);
        }
        else
        {
          return;
        }
      }
      --i_0;
    }
    RogueByte_List__insert__Byte_Int32( ROGUE_ARG(RogueStringBuilder_work_bytes), 49, 0 );
  }
}

RogueReal64 RogueStringBuilder__scan_work_bytes( RogueStringBuilder* THIS )
{
  ROGUE_GC_CHECK;
  RogueReal64 whole_0 = (0.0);
  RogueReal64 decimal_1 = (0.0);
  RogueInt32 decimal_count_2 = (0);
  RogueLogical scanning_whole_3 = (true);
  {
    ROGUE_DEF_LOCAL_REF(RogueByte_List*,_auto_317_0,(RogueStringBuilder_work_bytes));
    RogueInt32 _auto_318_0 = (0);
    RogueInt32 _auto_319_0 = (((_auto_317_0->count) - (1)));
    for (;ROGUE_COND(((_auto_318_0) <= (_auto_319_0)));++_auto_318_0)
    {
      ROGUE_GC_CHECK;
      RogueByte digit_0 = (_auto_317_0->data->as_bytes[_auto_318_0]);
      if (ROGUE_COND(scanning_whole_3))
      {
        if (ROGUE_COND(((((RogueCharacter)(digit_0))) == ((RogueCharacter)'.'))))
        {
          scanning_whole_3 = ((RogueLogical)(false));
        }
        else
        {
          whole_0 = ((RogueReal64)(((((whole_0) * (10.0))) + (((RogueReal64)(((((RogueCharacter)(digit_0))) - ((RogueCharacter)'0'))))))));
        }
      }
      else
      {
        decimal_1 = ((RogueReal64)(((((decimal_1) * (10.0))) + (((RogueReal64)(((((RogueCharacter)(digit_0))) - ((RogueCharacter)'0'))))))));
        ++decimal_count_2;
      }
    }
  }
  return (RogueReal64)(((whole_0) + (((decimal_1) / (((RogueReal64) pow((double)10.0, (double)((RogueReal64)(decimal_count_2)))))))));
}

RogueByte_List* RogueByte_List__init_object( RogueByte_List* THIS )
{
  ROGUE_GC_CHECK;
  RogueGenericList__init_object( ROGUE_ARG(((RogueClassGenericList*)THIS)) );
  return (RogueByte_List*)(THIS);
}

RogueByte_List* RogueByte_List__init( RogueByte_List* THIS )
{
  ROGUE_GC_CHECK;
  RogueByte_List__init__Int32( ROGUE_ARG(THIS), 0 );
  return (RogueByte_List*)(THIS);
}

RogueString* RogueByte_List__to_String( RogueByte_List* THIS )
{
  ROGUE_GC_CHECK;
  ROGUE_DEF_LOCAL_REF(RogueStringBuilder*,buffer_0,(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))));
  RogueStringBuilder__print__Character_Logical( buffer_0, (RogueCharacter)'[', true );
  RogueLogical first_1 = (true);
  {
    ROGUE_DEF_LOCAL_REF(RogueByte_List*,_auto_409_0,(THIS));
    RogueInt32 _auto_410_0 = (0);
    RogueInt32 _auto_411_0 = (((_auto_409_0->count) - (1)));
    for (;ROGUE_COND(((_auto_410_0) <= (_auto_411_0)));++_auto_410_0)
    {
      ROGUE_GC_CHECK;
      RogueByte value_0 = (_auto_409_0->data->as_bytes[_auto_410_0]);
      if (ROGUE_COND(first_1))
      {
        first_1 = ((RogueLogical)(false));
      }
      else
      {
        RogueStringBuilder__print__Character_Logical( buffer_0, (RogueCharacter)',', true );
      }
      if (ROGUE_COND((false)))
      {
      }
      else
      {
        RogueStringBuilder__print__String( buffer_0, ROGUE_ARG(((RogueString*)(RogueByte__to_String( value_0 )))) );
      }
    }
  }
  RogueStringBuilder__print__Character_Logical( buffer_0, (RogueCharacter)']', true );
  return (RogueString*)(((RogueString*)(RogueStringBuilder__to_String( buffer_0 ))));
}

RogueString* RogueByte_List__type_name( RogueByte_List* THIS )
{
  return (RogueString*)(Rogue_literal_strings[194]);
}

RogueByte_List* RogueByte_List__init__Int32( RogueByte_List* THIS, RogueInt32 initial_capacity_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(!!(initial_capacity_0)))
  {
    THIS->data = RogueType_create_array( initial_capacity_0, sizeof(RogueByte), false, 16 );
  }
  return (RogueByte_List*)(THIS);
}

RogueByte_List* RogueByte_List__add__Byte( RogueByte_List* THIS, RogueByte value_0 )
{
  ROGUE_GC_CHECK;
  ((RogueByte_List*)(RogueByte_List__reserve__Int32( ROGUE_ARG(THIS), 1 )))->data->as_bytes[THIS->count] = (value_0);
  THIS->count = ((THIS->count) + (1));
  return (RogueByte_List*)(THIS);
}

RogueInt32 RogueByte_List__capacity( RogueByte_List* THIS )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(!(!!(THIS->data))))
  {
    return (RogueInt32)(0);
  }
  return (RogueInt32)(THIS->data->count);
}

RogueByte_List* RogueByte_List__clear( RogueByte_List* THIS )
{
  ROGUE_GC_CHECK;
  RogueByte_List__discard_from__Int32( ROGUE_ARG(THIS), 0 );
  return (RogueByte_List*)(THIS);
}

RogueByte_List* RogueByte_List__ensure_capacity__Int32( RogueByte_List* THIS, RogueInt32 desired_capacity_0 )
{
  ROGUE_GC_CHECK;
  return (RogueByte_List*)(((RogueByte_List*)(RogueByte_List__reserve__Int32( ROGUE_ARG(THIS), ROGUE_ARG(((desired_capacity_0) - (THIS->count))) ))));
}

RogueByte_List* RogueByte_List__expand_to_count__Int32( RogueByte_List* THIS, RogueInt32 minimum_count_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((THIS->count) < (minimum_count_0))))
  {
    RogueByte_List__ensure_capacity__Int32( ROGUE_ARG(THIS), minimum_count_0 );
    THIS->count = minimum_count_0;
  }
  return (RogueByte_List*)(THIS);
}

RogueByte_List* RogueByte_List__expand_to_include__Int32( RogueByte_List* THIS, RogueInt32 index_0 )
{
  ROGUE_GC_CHECK;
  return (RogueByte_List*)(((RogueByte_List*)(RogueByte_List__expand_to_count__Int32( ROGUE_ARG(THIS), ROGUE_ARG(((index_0) + (1))) ))));
}

RogueByte_List* RogueByte_List__discard_from__Int32( RogueByte_List* THIS, RogueInt32 index_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(!!(THIS->data)))
  {
    RogueInt32 n_1 = (((THIS->count) - (index_0)));
    if (ROGUE_COND(((n_1) > (0))))
    {
      RogueArray__zero__Int32_Int32( ROGUE_ARG(((RogueArray*)THIS->data)), index_0, n_1 );
      THIS->count = index_0;
    }
  }
  return (RogueByte_List*)(THIS);
}

RogueByte_List* RogueByte_List__insert__Byte_Int32( RogueByte_List* THIS, RogueByte value_0, RogueInt32 before_index_1 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((before_index_1) < (0))))
  {
    before_index_1 = ((RogueInt32)(0));
  }
  if (ROGUE_COND(((before_index_1) >= (THIS->count))))
  {
    return (RogueByte_List*)(((RogueByte_List*)(RogueByte_List__add__Byte( ROGUE_ARG(THIS), value_0 ))));
  }
  else
  {
    RogueByte_List__reserve__Int32( ROGUE_ARG(THIS), 1 );
    RogueByte_List__shift__Int32_OptionalInt32_Int32_OptionalByte( ROGUE_ARG(THIS), before_index_1, (RogueOptionalInt32__create()), 1, (RogueOptionalByte__create()) );
    THIS->data->as_bytes[before_index_1] = (value_0);
  }
  return (RogueByte_List*)(THIS);
}

RogueByte_List* RogueByte_List__reserve__Int32( RogueByte_List* THIS, RogueInt32 additional_elements_0 )
{
  ROGUE_GC_CHECK;
  RogueInt32 required_capacity_1 = (((THIS->count) + (additional_elements_0)));
  if (ROGUE_COND(!(!!(THIS->data))))
  {
    if (ROGUE_COND(((required_capacity_1) < (10))))
    {
      required_capacity_1 = ((RogueInt32)(10));
    }
    THIS->data = RogueType_create_array( required_capacity_1, sizeof(RogueByte), false, 16 );
  }
  else if (ROGUE_COND(((required_capacity_1) > (THIS->data->count))))
  {
    RogueInt32 cap_2 = (((RogueByte_List__capacity( ROGUE_ARG(THIS) ))));
    if (ROGUE_COND(((required_capacity_1) < (((cap_2) + (cap_2))))))
    {
      required_capacity_1 = ((RogueInt32)(((cap_2) + (cap_2))));
    }
    ROGUE_DEF_LOCAL_REF(RogueArray*,new_data_3,(RogueType_create_array( required_capacity_1, sizeof(RogueByte), false, 16 )));
    RogueArray_set(new_data_3,0,((RogueArray*)(THIS->data)),0,-1);
    THIS->data = new_data_3;
  }
  return (RogueByte_List*)(THIS);
}

RogueByte RogueByte_List__remove_at__Int32( RogueByte_List* THIS, RogueInt32 index_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((RogueLogical)((((unsigned int)index_0) >= (unsigned int)THIS->count)))))
  {
    throw ((RogueClassOutOfBoundsError*)(RogueOutOfBoundsError___throw( ROGUE_ARG(((RogueClassOutOfBoundsError*)(RogueOutOfBoundsError__init__Int32_Int32( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassOutOfBoundsError*,ROGUE_CREATE_OBJECT(OutOfBoundsError))), index_0, ROGUE_ARG(THIS->count) )))) )));
  }
  RogueByte result_1 = (THIS->data->as_bytes[index_0]);
  RogueArray_set(THIS->data,index_0,((RogueArray*)(THIS->data)),((index_0) + (1)),-1);
  RogueByte zero_value_2 = 0;
  THIS->count = ((THIS->count) + (-1));
  THIS->data->as_bytes[THIS->count] = (zero_value_2);
  return (RogueByte)(result_1);
}

RogueByte RogueByte_List__remove_last( RogueByte_List* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueByte)(((RogueByte_List__remove_at__Int32( ROGUE_ARG(THIS), ROGUE_ARG(((THIS->count) - (1))) ))));
}

RogueByte_List* RogueByte_List__reverse( RogueByte_List* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueByte_List*)(((RogueByte_List*)(RogueByte_List__reverse__Int32_Int32( ROGUE_ARG(THIS), 0, ROGUE_ARG(((THIS->count) - (1))) ))));
}

RogueByte_List* RogueByte_List__reverse__Int32_Int32( RogueByte_List* THIS, RogueInt32 i1_0, RogueInt32 i2_1 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((i1_0) < (0))))
  {
    i1_0 = ((RogueInt32)(0));
  }
  if (ROGUE_COND(((i2_1) >= (THIS->count))))
  {
    i2_1 = ((RogueInt32)(((THIS->count) - (1))));
  }
  ROGUE_DEF_LOCAL_REF(RogueArray*,_data_2,(THIS->data));
  while (ROGUE_COND(((i1_0) < (i2_1))))
  {
    ROGUE_GC_CHECK;
    RogueByte temp_3 = (_data_2->as_bytes[i1_0]);
    _data_2->as_bytes[i1_0] = (_data_2->as_bytes[i2_1]);
    _data_2->as_bytes[i2_1] = (temp_3);
    ++i1_0;
    --i2_1;
  }
  return (RogueByte_List*)(THIS);
}

RogueByte_List* RogueByte_List__shift__Int32_OptionalInt32_Int32_OptionalByte( RogueByte_List* THIS, RogueInt32 i1_0, RogueOptionalInt32 element_count_1, RogueInt32 delta_2, RogueOptionalByte fill_3 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((delta_2) == (0))))
  {
    return (RogueByte_List*)(THIS);
  }
  RogueInt32 n_4 = 0;
  if (ROGUE_COND(element_count_1.exists))
  {
    n_4 = ((RogueInt32)(element_count_1.value));
  }
  else
  {
    n_4 = ((RogueInt32)(((THIS->count) - (i1_0))));
  }
  RogueInt32 dest_i2_5 = (((((((i1_0) + (delta_2))) + (n_4))) - (1)));
  RogueByte_List__expand_to_include__Int32( ROGUE_ARG(THIS), dest_i2_5 );
  RogueArray_set(THIS->data,((i1_0) + (delta_2)),((RogueArray*)(THIS->data)),i1_0,n_4);
  if (ROGUE_COND(fill_3.exists))
  {
    RogueByte value_6 = (fill_3.value);
    if (ROGUE_COND(((delta_2) > (0))))
    {
      {
        RogueInt32 i_7 = (i1_0);
        RogueInt32 _auto_41_8 = (((((i1_0) + (delta_2))) - (1)));
        for (;ROGUE_COND(((i_7) <= (_auto_41_8)));++i_7)
        {
          ROGUE_GC_CHECK;
          THIS->data->as_bytes[i_7] = (value_6);
        }
      }
    }
    else
    {
      {
        RogueInt32 i_9 = (((((i1_0) + (delta_2))) + (n_4)));
        RogueInt32 _auto_42_10 = (((((i1_0) + (n_4))) - (1)));
        for (;ROGUE_COND(((i_9) <= (_auto_42_10)));++i_9)
        {
          ROGUE_GC_CHECK;
          THIS->data->as_bytes[i_9] = (value_6);
        }
      }
    }
  }
  return (RogueByte_List*)(THIS);
}

RogueClassGenericList* RogueGenericList__init_object( RogueClassGenericList* THIS )
{
  ROGUE_GC_CHECK;
  RogueObject__init_object( ROGUE_ARG(((RogueObject*)THIS)) );
  return (RogueClassGenericList*)(THIS);
}

RogueString* RogueGenericList__type_name( RogueClassGenericList* THIS )
{
  return (RogueString*)(Rogue_literal_strings[138]);
}

RogueString* RogueByte__to_String( RogueByte THIS )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(((RogueString*)(RogueString__operatorPLUS__Int32( ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], Rogue_literal_strings[0] ))) ))), ROGUE_ARG(((RogueInt32)(THIS))) ))));
}

RogueString* RogueArray_Byte___type_name( RogueArray* THIS )
{
  return (RogueString*)(Rogue_literal_strings[186]);
}

RogueClassStringBuilderPool* RogueStringBuilderPool__init_object( RogueClassStringBuilderPool* THIS )
{
  ROGUE_GC_CHECK;
  RogueObject__init_object( ROGUE_ARG(((RogueObject*)THIS)) );
  THIS->available = ((RogueStringBuilder_List*)(RogueStringBuilder_List__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder_List*,ROGUE_CREATE_OBJECT(StringBuilder_List))) )));
  return (RogueClassStringBuilderPool*)(THIS);
}

RogueString* RogueStringBuilderPool__type_name( RogueClassStringBuilderPool* THIS )
{
  return (RogueString*)(Rogue_literal_strings[156]);
}

RogueStringBuilder_List* RogueStringBuilder_List__init_object( RogueStringBuilder_List* THIS )
{
  ROGUE_GC_CHECK;
  RogueGenericList__init_object( ROGUE_ARG(((RogueClassGenericList*)THIS)) );
  return (RogueStringBuilder_List*)(THIS);
}

RogueStringBuilder_List* RogueStringBuilder_List__init( RogueStringBuilder_List* THIS )
{
  ROGUE_GC_CHECK;
  RogueStringBuilder_List__init__Int32( ROGUE_ARG(THIS), 0 );
  return (RogueStringBuilder_List*)(THIS);
}

RogueString* RogueStringBuilder_List__to_String( RogueStringBuilder_List* THIS )
{
  ROGUE_GC_CHECK;
  ROGUE_DEF_LOCAL_REF(RogueStringBuilder*,buffer_0,(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))));
  RogueStringBuilder__print__Character_Logical( buffer_0, (RogueCharacter)'[', true );
  RogueLogical first_1 = (true);
  {
    ROGUE_DEF_LOCAL_REF(RogueStringBuilder_List*,_auto_482_0,(THIS));
    RogueInt32 _auto_483_0 = (0);
    RogueInt32 _auto_484_0 = (((_auto_482_0->count) - (1)));
    for (;ROGUE_COND(((_auto_483_0) <= (_auto_484_0)));++_auto_483_0)
    {
      ROGUE_GC_CHECK;
      ROGUE_DEF_LOCAL_REF(RogueStringBuilder*,value_0,(((RogueStringBuilder*)(_auto_482_0->data->as_objects[_auto_483_0]))));
      if (ROGUE_COND(first_1))
      {
        first_1 = ((RogueLogical)(false));
      }
      else
      {
        RogueStringBuilder__print__Character_Logical( buffer_0, (RogueCharacter)',', true );
      }
      if (ROGUE_COND(((void*)value_0) == ((void*)NULL)))
      {
        RogueStringBuilder__print__String( buffer_0, Rogue_literal_strings[1] );
      }
      else
      {
        RogueStringBuilder__print__String( buffer_0, ROGUE_ARG(((RogueString*)(RogueStringBuilder__to_String( value_0 )))) );
      }
    }
  }
  RogueStringBuilder__print__Character_Logical( buffer_0, (RogueCharacter)']', true );
  return (RogueString*)(((RogueString*)(RogueStringBuilder__to_String( buffer_0 ))));
}

RogueString* RogueStringBuilder_List__type_name( RogueStringBuilder_List* THIS )
{
  return (RogueString*)(Rogue_literal_strings[199]);
}

RogueStringBuilder_List* RogueStringBuilder_List__init__Int32( RogueStringBuilder_List* THIS, RogueInt32 initial_capacity_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(!!(initial_capacity_0)))
  {
    THIS->data = RogueType_create_array( initial_capacity_0, sizeof(RogueStringBuilder*), true, 13 );
  }
  return (RogueStringBuilder_List*)(THIS);
}

RogueString* RogueArray_StringBuilder___type_name( RogueArray* THIS )
{
  return (RogueString*)(Rogue_literal_strings[188]);
}

RogueString* RogueLogical__to_String( RogueLogical THIS )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(((RogueString*)(RogueString__operatorPLUS__Logical( ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], Rogue_literal_strings[0] ))), ROGUE_ARG(THIS) ))));
}

Rogue_Function____List* Rogue_Function____List__init_object( Rogue_Function____List* THIS )
{
  ROGUE_GC_CHECK;
  RogueGenericList__init_object( ROGUE_ARG(((RogueClassGenericList*)THIS)) );
  return (Rogue_Function____List*)(THIS);
}

Rogue_Function____List* Rogue_Function____List__init( Rogue_Function____List* THIS )
{
  ROGUE_GC_CHECK;
  Rogue_Function____List__init__Int32( ROGUE_ARG(THIS), 0 );
  return (Rogue_Function____List*)(THIS);
}

RogueString* Rogue_Function____List__to_String( Rogue_Function____List* THIS )
{
  ROGUE_GC_CHECK;
  ROGUE_DEF_LOCAL_REF(RogueStringBuilder*,buffer_0,(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))));
  RogueStringBuilder__print__Character_Logical( buffer_0, (RogueCharacter)'[', true );
  RogueLogical first_1 = (true);
  {
    ROGUE_DEF_LOCAL_REF(Rogue_Function____List*,_auto_558_0,(THIS));
    RogueInt32 _auto_559_0 = (0);
    RogueInt32 _auto_560_0 = (((_auto_558_0->count) - (1)));
    for (;ROGUE_COND(((_auto_559_0) <= (_auto_560_0)));++_auto_559_0)
    {
      ROGUE_GC_CHECK;
      ROGUE_DEF_LOCAL_REF(RogueClass_Function___*,value_0,(((RogueClass_Function___*)(_auto_558_0->data->as_objects[_auto_559_0]))));
      if (ROGUE_COND(first_1))
      {
        first_1 = ((RogueLogical)(false));
      }
      else
      {
        RogueStringBuilder__print__Character_Logical( buffer_0, (RogueCharacter)',', true );
      }
      if (ROGUE_COND(((void*)value_0) == ((void*)NULL)))
      {
        RogueStringBuilder__print__String( buffer_0, Rogue_literal_strings[1] );
      }
      else
      {
        RogueStringBuilder__print__String( buffer_0, ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ((RogueObject*)value_0) ))) );
      }
    }
  }
  RogueStringBuilder__print__Character_Logical( buffer_0, (RogueCharacter)']', true );
  return (RogueString*)(((RogueString*)(RogueStringBuilder__to_String( buffer_0 ))));
}

RogueString* Rogue_Function____List__type_name( Rogue_Function____List* THIS )
{
  return (RogueString*)(Rogue_literal_strings[198]);
}

Rogue_Function____List* Rogue_Function____List__init__Int32( Rogue_Function____List* THIS, RogueInt32 initial_capacity_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(!!(initial_capacity_0)))
  {
    THIS->data = RogueType_create_array( initial_capacity_0, sizeof(RogueClass_Function___*), true, 23 );
  }
  return (Rogue_Function____List*)(THIS);
}

Rogue_Function____List* Rogue_Function____List__add___Function___( Rogue_Function____List* THIS, RogueClass_Function___* value_0 )
{
  ROGUE_GC_CHECK;
  ((Rogue_Function____List*)(Rogue_Function____List__reserve__Int32( ROGUE_ARG(THIS), 1 )))->data->as_objects[THIS->count] = value_0;
  THIS->count = ((THIS->count) + (1));
  return (Rogue_Function____List*)(THIS);
}

RogueInt32 Rogue_Function____List__capacity( Rogue_Function____List* THIS )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(!(!!(THIS->data))))
  {
    return (RogueInt32)(0);
  }
  return (RogueInt32)(THIS->data->count);
}

Rogue_Function____List* Rogue_Function____List__reserve__Int32( Rogue_Function____List* THIS, RogueInt32 additional_elements_0 )
{
  ROGUE_GC_CHECK;
  RogueInt32 required_capacity_1 = (((THIS->count) + (additional_elements_0)));
  if (ROGUE_COND(!(!!(THIS->data))))
  {
    if (ROGUE_COND(((required_capacity_1) < (10))))
    {
      required_capacity_1 = ((RogueInt32)(10));
    }
    THIS->data = RogueType_create_array( required_capacity_1, sizeof(RogueClass_Function___*), true, 23 );
  }
  else if (ROGUE_COND(((required_capacity_1) > (THIS->data->count))))
  {
    RogueInt32 cap_2 = (((Rogue_Function____List__capacity( ROGUE_ARG(THIS) ))));
    if (ROGUE_COND(((required_capacity_1) < (((cap_2) + (cap_2))))))
    {
      required_capacity_1 = ((RogueInt32)(((cap_2) + (cap_2))));
    }
    ROGUE_DEF_LOCAL_REF(RogueArray*,new_data_3,(RogueType_create_array( required_capacity_1, sizeof(RogueClass_Function___*), true, 23 )));
    RogueArray_set(new_data_3,0,((RogueArray*)(THIS->data)),0,-1);
    THIS->data = new_data_3;
  }
  return (Rogue_Function____List*)(THIS);
}

RogueClass_Function___* Rogue_Function_____init_object( RogueClass_Function___* THIS )
{
  ROGUE_GC_CHECK;
  RogueObject__init_object( ROGUE_ARG(((RogueObject*)THIS)) );
  return (RogueClass_Function___*)(THIS);
}

RogueString* Rogue_Function_____type_name( RogueClass_Function___* THIS )
{
  return (RogueString*)(Rogue_literal_strings[144]);
}

void Rogue_Function_____call( RogueClass_Function___* THIS )
{
}

RogueString* RogueArray__Function______type_name( RogueArray* THIS )
{
  return (RogueString*)(Rogue_literal_strings[185]);
}

RogueException* RogueException__init_object( RogueException* THIS )
{
  ROGUE_GC_CHECK;
  RogueObject__init_object( ROGUE_ARG(((RogueObject*)THIS)) );
  THIS->stack_trace = ((RogueClassStackTrace*)(RogueStackTrace__init__Int32( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassStackTrace*,ROGUE_CREATE_OBJECT(StackTrace))), 1 )));
  return (RogueException*)(THIS);
}

RogueException* RogueException__init( RogueException* THIS )
{
  ROGUE_GC_CHECK;
  THIS->message = ((RogueString*)Rogue_call_ROGUEM1( 12, ROGUE_ARG(THIS) ));
  return (RogueException*)(THIS);
}

RogueString* RogueException__to_String( RogueException* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(THIS->message);
}

RogueString* RogueException__type_name( RogueException* THIS )
{
  return (RogueString*)(Rogue_literal_strings[4]);
}

RogueException* RogueException__init__String( RogueException* THIS, RogueString* _auto_75_0 )
{
  ROGUE_GC_CHECK;
  THIS->message = _auto_75_0;
  return (RogueException*)(THIS);
}

void RogueException__display( RogueException* THIS )
{
  ROGUE_GC_CHECK;
  ROGUE_DEF_LOCAL_REF(RogueStringBuilder*,builder_0,(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))));
  RogueStringBuilder__println__String( builder_0, ROGUE_ARG((RogueString__operatorTIMES__String_Int32( Rogue_literal_strings[5], ROGUE_ARG(((RogueInt32__or_smaller__Int32( ROGUE_ARG(((((RogueConsole__width( ((RogueClassConsole*)ROGUE_SINGLETON(Console)) )))) - (1))), 79 )))) ))) );
  RogueStringBuilder__println__String( builder_0, ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 12, ROGUE_ARG(THIS) ))) );
  RogueStringBuilder__println__String( builder_0, ROGUE_ARG(((RogueString*)(RogueString_List__join__String( ROGUE_ARG(((RogueString_List*)(RogueString__word_wrap__Int32_String( ROGUE_ARG(THIS->message), 79, Rogue_literal_strings[6] )))), Rogue_literal_strings[7] )))) );
  if (ROGUE_COND(!!(THIS->stack_trace->entries->count)))
  {
    RogueStringBuilder__println( builder_0 );
    RogueStringBuilder__println__String( builder_0, ROGUE_ARG(((RogueString*)(RogueString__trimmed( ROGUE_ARG(((RogueString*)(RogueStackTrace__to_String( ROGUE_ARG(THIS->stack_trace) )))) )))) );
  }
  RogueStringBuilder__println__String( builder_0, ROGUE_ARG((RogueString__operatorTIMES__String_Int32( Rogue_literal_strings[5], ROGUE_ARG(((RogueInt32__or_smaller__Int32( ROGUE_ARG(((((RogueConsole__width( ((RogueClassConsole*)ROGUE_SINGLETON(Console)) )))) - (1))), 79 )))) ))) );
  RogueGlobal__println__Object( ((RogueClassGlobal*)ROGUE_SINGLETON(Global)), ROGUE_ARG(((RogueObject*)(builder_0))) );
}

RogueString* RogueException__format( RogueException* THIS )
{
  ROGUE_GC_CHECK;
  ROGUE_DEF_LOCAL_REF(RogueString*,st_0,(((RogueString*)(RogueStackTrace__to_String( ROGUE_ARG(THIS->stack_trace) )))));
  if (ROGUE_COND(((void*)st_0) == ((void*)NULL)))
  {
    st_0 = ((RogueString*)(Rogue_literal_strings[12]));
  }
  st_0 = ((RogueString*)(((RogueString*)(RogueString__trimmed( st_0 )))));
  if (ROGUE_COND(!!(st_0->character_count)))
  {
    st_0 = ((RogueString*)((RogueString__operatorPLUS__String_String( Rogue_literal_strings[7], st_0 ))));
  }
  return (RogueString*)((RogueString__operatorPLUS__String_String( ROGUE_ARG(((((THIS))) ? (ROGUE_ARG(((RogueString*)(RogueException__to_String( ROGUE_ARG(THIS) ))))) : ROGUE_ARG(Rogue_literal_strings[13]))), st_0 )));
}

RogueClassStackTrace* RogueStackTrace__init_object( RogueClassStackTrace* THIS )
{
  ROGUE_GC_CHECK;
  RogueObject__init_object( ROGUE_ARG(((RogueObject*)THIS)) );
  return (RogueClassStackTrace*)(THIS);
}

RogueString* RogueStackTrace__to_String( RogueClassStackTrace* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStackTrace__print__StringBuilder( ROGUE_ARG(THIS), ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))) )))) ))));
}

RogueString* RogueStackTrace__type_name( RogueClassStackTrace* THIS )
{
  return (RogueString*)(Rogue_literal_strings[141]);
}

RogueClassStackTrace* RogueStackTrace__init__Int32( RogueClassStackTrace* THIS, RogueInt32 omit_count_0 )
{
  ROGUE_GC_CHECK;
  ++omit_count_0;
  RogueDebugTrace* current = Rogue_call_stack;
  while (current && omit_count_0 > 0)
  {
    --omit_count_0;
    current = current->previous_trace;
  }
  if (current) THIS->count = current->count();

  THIS->entries = ((RogueString_List*)(RogueString_List__init__Int32( ROGUE_ARG(ROGUE_CREATE_REF(RogueString_List*,ROGUE_CREATE_OBJECT(String_List))), ROGUE_ARG(THIS->count) )));
  while (ROGUE_COND(((RogueLogical)(current!=0))))
  {
    ROGUE_GC_CHECK;
    RogueString_List__add__String( ROGUE_ARG(THIS->entries), ROGUE_ARG(((RogueString*)(RogueString_create_from_utf8( current->to_c_string() )))) );
    current = current->previous_trace;

  }
  return (RogueClassStackTrace*)(THIS);
}

void RogueStackTrace__format( RogueClassStackTrace* THIS )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(THIS->is_formatted))
  {
    return;
  }
  THIS->is_formatted = true;
  RogueInt32 max_characters_0 = (0);
  {
    ROGUE_DEF_LOCAL_REF(RogueString_List*,_auto_287_0,(THIS->entries));
    RogueInt32 _auto_288_0 = (0);
    RogueInt32 _auto_289_0 = (((_auto_287_0->count) - (1)));
    for (;ROGUE_COND(((_auto_288_0) <= (_auto_289_0)));++_auto_288_0)
    {
      ROGUE_GC_CHECK;
      ROGUE_DEF_LOCAL_REF(RogueString*,entry_0,(((RogueString*)(_auto_287_0->data->as_objects[_auto_288_0]))));
      RogueOptionalInt32 sp_1 = (((RogueString__locate__Character_OptionalInt32( entry_0, (RogueCharacter)' ', (RogueOptionalInt32__create()) ))));
      if (ROGUE_COND(sp_1.exists))
      {
        max_characters_0 = ((RogueInt32)((RogueMath__max__Int32_Int32( max_characters_0, ROGUE_ARG(sp_1.value) ))));
      }
    }
  }
  ++max_characters_0;
  {
    ROGUE_DEF_LOCAL_REF(RogueString_List*,_auto_290_0,(THIS->entries));
    RogueInt32 i_0 = (0);
    RogueInt32 _auto_291_0 = (((_auto_290_0->count) - (1)));
    for (;ROGUE_COND(((i_0) <= (_auto_291_0)));++i_0)
    {
      ROGUE_GC_CHECK;
      ROGUE_DEF_LOCAL_REF(RogueString*,entry_0,(((RogueString*)(_auto_290_0->data->as_objects[i_0]))));
      if (ROGUE_COND(((RogueString__contains__Character( entry_0, (RogueCharacter)' ' )))))
      {
        THIS->entries->data->as_objects[i_0] = (RogueString__operatorPLUS__String_String( ROGUE_ARG(((RogueString*)(RogueString__left_justified__Int32_Character( ROGUE_ARG(((RogueString*)(RogueString__before_first__Character( entry_0, (RogueCharacter)' ' )))), max_characters_0, (RogueCharacter)' ' )))), ROGUE_ARG(((RogueString*)(RogueString__from_first__Character( entry_0, (RogueCharacter)' ' )))) ));
      }
    }
  }
}

void RogueStackTrace__print( RogueClassStackTrace* THIS )
{
  ROGUE_GC_CHECK;
  RogueStackTrace__print__StringBuilder( ROGUE_ARG(THIS), ROGUE_ARG(((RogueClassGlobal*)ROGUE_SINGLETON(Global))->global_output_buffer) );
  RogueGlobal__flush( ((RogueClassGlobal*)ROGUE_SINGLETON(Global)) );
}

RogueStringBuilder* RogueStackTrace__print__StringBuilder( RogueClassStackTrace* THIS, RogueStringBuilder* buffer_0 )
{
  ROGUE_GC_CHECK;
  RogueStackTrace__format( ROGUE_ARG(THIS) );
  {
    ROGUE_DEF_LOCAL_REF(RogueString_List*,_auto_292_0,(THIS->entries));
    RogueInt32 _auto_293_0 = (0);
    RogueInt32 _auto_294_0 = (((_auto_292_0->count) - (1)));
    for (;ROGUE_COND(((_auto_293_0) <= (_auto_294_0)));++_auto_293_0)
    {
      ROGUE_GC_CHECK;
      ROGUE_DEF_LOCAL_REF(RogueString*,entry_0,(((RogueString*)(_auto_292_0->data->as_objects[_auto_293_0]))));
      RogueStringBuilder__println__String( buffer_0, entry_0 );
    }
  }
  return (RogueStringBuilder*)(buffer_0);
}

RogueString_List* RogueString_List__init_object( RogueString_List* THIS )
{
  ROGUE_GC_CHECK;
  RogueGenericList__init_object( ROGUE_ARG(((RogueClassGenericList*)THIS)) );
  return (RogueString_List*)(THIS);
}

RogueString_List* RogueString_List__init( RogueString_List* THIS )
{
  ROGUE_GC_CHECK;
  RogueString_List__init__Int32( ROGUE_ARG(THIS), 0 );
  return (RogueString_List*)(THIS);
}

RogueString* RogueString_List__to_String( RogueString_List* THIS )
{
  ROGUE_GC_CHECK;
  ROGUE_DEF_LOCAL_REF(RogueStringBuilder*,buffer_0,(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))));
  RogueStringBuilder__print__Character_Logical( buffer_0, (RogueCharacter)'[', true );
  RogueLogical first_1 = (true);
  {
    ROGUE_DEF_LOCAL_REF(RogueString_List*,_auto_634_0,(THIS));
    RogueInt32 _auto_635_0 = (0);
    RogueInt32 _auto_636_0 = (((_auto_634_0->count) - (1)));
    for (;ROGUE_COND(((_auto_635_0) <= (_auto_636_0)));++_auto_635_0)
    {
      ROGUE_GC_CHECK;
      ROGUE_DEF_LOCAL_REF(RogueString*,value_0,(((RogueString*)(_auto_634_0->data->as_objects[_auto_635_0]))));
      if (ROGUE_COND(first_1))
      {
        first_1 = ((RogueLogical)(false));
      }
      else
      {
        RogueStringBuilder__print__Character_Logical( buffer_0, (RogueCharacter)',', true );
      }
      if (ROGUE_COND(((void*)value_0) == ((void*)NULL)))
      {
        RogueStringBuilder__print__String( buffer_0, Rogue_literal_strings[1] );
      }
      else
      {
        RogueStringBuilder__print__String( buffer_0, value_0 );
      }
    }
  }
  RogueStringBuilder__print__Character_Logical( buffer_0, (RogueCharacter)']', true );
  return (RogueString*)(((RogueString*)(RogueStringBuilder__to_String( buffer_0 ))));
}

RogueString* RogueString_List__type_name( RogueString_List* THIS )
{
  return (RogueString*)(Rogue_literal_strings[197]);
}

RogueString_List* RogueString_List__init__Int32( RogueString_List* THIS, RogueInt32 initial_capacity_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(!!(initial_capacity_0)))
  {
    THIS->data = RogueType_create_array( initial_capacity_0, sizeof(RogueString*), true, 11 );
  }
  return (RogueString_List*)(THIS);
}

RogueString_List* RogueString_List__add__String( RogueString_List* THIS, RogueString* value_0 )
{
  ROGUE_GC_CHECK;
  ((RogueString_List*)(RogueString_List__reserve__Int32( ROGUE_ARG(THIS), 1 )))->data->as_objects[THIS->count] = value_0;
  THIS->count = ((THIS->count) + (1));
  return (RogueString_List*)(THIS);
}

RogueInt32 RogueString_List__capacity( RogueString_List* THIS )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(!(!!(THIS->data))))
  {
    return (RogueInt32)(0);
  }
  return (RogueInt32)(THIS->data->count);
}

RogueOptionalInt32 RogueString_List__locate__String( RogueString_List* THIS, RogueString* value_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(true))
  {
    if (ROGUE_COND(((void*)value_0) == ((void*)NULL)))
    {
      {
        ROGUE_DEF_LOCAL_REF(RogueString_List*,_auto_673_0,(THIS));
        RogueInt32 i_0 = (0);
        RogueInt32 _auto_674_0 = (((_auto_673_0->count) - (1)));
        for (;ROGUE_COND(((i_0) <= (_auto_674_0)));++i_0)
        {
          ROGUE_GC_CHECK;
          if (ROGUE_COND(((void*)value_0) == ((void*)((RogueString*)(THIS->data->as_objects[i_0])))))
          {
            return (RogueOptionalInt32)(RogueOptionalInt32( i_0, true ));
          }
        }
      }
      return (RogueOptionalInt32)((RogueOptionalInt32__create()));
    }
  }
  {
    ROGUE_DEF_LOCAL_REF(RogueString_List*,_auto_675_0,(THIS));
    RogueInt32 i_0 = (0);
    RogueInt32 _auto_676_0 = (((_auto_675_0->count) - (1)));
    for (;ROGUE_COND(((i_0) <= (_auto_676_0)));++i_0)
    {
      ROGUE_GC_CHECK;
      if (ROGUE_COND((RogueString__operatorEQUALSEQUALS__String_String( value_0, ROGUE_ARG(((RogueString*)(THIS->data->as_objects[i_0]))) ))))
      {
        return (RogueOptionalInt32)(RogueOptionalInt32( i_0, true ));
      }
    }
  }
  return (RogueOptionalInt32)((RogueOptionalInt32__create()));
}

RogueString_List* RogueString_List__reserve__Int32( RogueString_List* THIS, RogueInt32 additional_elements_0 )
{
  ROGUE_GC_CHECK;
  RogueInt32 required_capacity_1 = (((THIS->count) + (additional_elements_0)));
  if (ROGUE_COND(!(!!(THIS->data))))
  {
    if (ROGUE_COND(((required_capacity_1) < (10))))
    {
      required_capacity_1 = ((RogueInt32)(10));
    }
    THIS->data = RogueType_create_array( required_capacity_1, sizeof(RogueString*), true, 11 );
  }
  else if (ROGUE_COND(((required_capacity_1) > (THIS->data->count))))
  {
    RogueInt32 cap_2 = (((RogueString_List__capacity( ROGUE_ARG(THIS) ))));
    if (ROGUE_COND(((required_capacity_1) < (((cap_2) + (cap_2))))))
    {
      required_capacity_1 = ((RogueInt32)(((cap_2) + (cap_2))));
    }
    ROGUE_DEF_LOCAL_REF(RogueArray*,new_data_3,(RogueType_create_array( required_capacity_1, sizeof(RogueString*), true, 11 )));
    RogueArray_set(new_data_3,0,((RogueArray*)(THIS->data)),0,-1);
    THIS->data = new_data_3;
  }
  return (RogueString_List*)(THIS);
}

RogueString* RogueString_List__remove__String( RogueString_List* THIS, RogueString* value_0 )
{
  ROGUE_GC_CHECK;
  RogueOptionalInt32 index_1 = (((RogueString_List__locate__String( ROGUE_ARG(THIS), value_0 ))));
  if (ROGUE_COND(index_1.exists))
  {
    return (RogueString*)(((RogueString*)(RogueString_List__remove_at__Int32( ROGUE_ARG(THIS), ROGUE_ARG(index_1.value) ))));
  }
  else
  {
    return (RogueString*)(value_0);
  }
}

RogueString* RogueString_List__remove_at__Int32( RogueString_List* THIS, RogueInt32 index_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((RogueLogical)((((unsigned int)index_0) >= (unsigned int)THIS->count)))))
  {
    throw ((RogueClassOutOfBoundsError*)(RogueOutOfBoundsError___throw( ROGUE_ARG(((RogueClassOutOfBoundsError*)(RogueOutOfBoundsError__init__Int32_Int32( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassOutOfBoundsError*,ROGUE_CREATE_OBJECT(OutOfBoundsError))), index_0, ROGUE_ARG(THIS->count) )))) )));
  }
  ROGUE_DEF_LOCAL_REF(RogueString*,result_1,(((RogueString*)(THIS->data->as_objects[index_0]))));
  RogueArray_set(THIS->data,index_0,((RogueArray*)(THIS->data)),((index_0) + (1)),-1);
  ROGUE_DEF_LOCAL_REF(RogueString*,zero_value_2,0);
  THIS->count = ((THIS->count) + (-1));
  THIS->data->as_objects[THIS->count] = zero_value_2;
  return (RogueString*)(result_1);
}

RogueString* RogueString_List__join__String( RogueString_List* THIS, RogueString* separator_0 )
{
  ROGUE_GC_CHECK;
  RogueInt32 total_count_1 = (0);
  {
    ROGUE_DEF_LOCAL_REF(RogueString_List*,_auto_697_0,(THIS));
    RogueInt32 _auto_698_0 = (0);
    RogueInt32 _auto_699_0 = (((_auto_697_0->count) - (1)));
    for (;ROGUE_COND(((_auto_698_0) <= (_auto_699_0)));++_auto_698_0)
    {
      ROGUE_GC_CHECK;
      ROGUE_DEF_LOCAL_REF(RogueString*,line_0,(((RogueString*)(_auto_697_0->data->as_objects[_auto_698_0]))));
      total_count_1 += line_0->character_count;
    }
  }
  ROGUE_DEF_LOCAL_REF(RogueStringBuilder*,builder_2,(((RogueStringBuilder*)(RogueStringBuilder__init__Int32( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))), total_count_1 )))));
  RogueLogical first_3 = (true);
  {
    ROGUE_DEF_LOCAL_REF(RogueString_List*,_auto_700_0,(THIS));
    RogueInt32 _auto_701_0 = (0);
    RogueInt32 _auto_702_0 = (((_auto_700_0->count) - (1)));
    for (;ROGUE_COND(((_auto_701_0) <= (_auto_702_0)));++_auto_701_0)
    {
      ROGUE_GC_CHECK;
      ROGUE_DEF_LOCAL_REF(RogueString*,line_0,(((RogueString*)(_auto_700_0->data->as_objects[_auto_701_0]))));
      if (ROGUE_COND(first_3))
      {
        first_3 = ((RogueLogical)(false));
      }
      else
      {
        RogueStringBuilder__print__String( builder_2, separator_0 );
      }
      RogueStringBuilder__print__String( builder_2, line_0 );
    }
  }
  return (RogueString*)(((RogueString*)(RogueStringBuilder__to_String( builder_2 ))));
}

RogueString_List* RogueString_List__mapped_String____Function_String_RETURNSString_( RogueString_List* THIS, RogueClass_Function_String_RETURNSString_* map_fn_0 )
{
  ROGUE_GC_CHECK;
  ROGUE_DEF_LOCAL_REF(RogueString_List*,result_1,(((RogueString_List*)(RogueString_List__init__Int32( ROGUE_ARG(ROGUE_CREATE_REF(RogueString_List*,ROGUE_CREATE_OBJECT(String_List))), ROGUE_ARG(((RogueString_List__capacity( ROGUE_ARG(THIS) )))) )))));
  {
    ROGUE_DEF_LOCAL_REF(RogueString_List*,_auto_704_0,(THIS));
    RogueInt32 _auto_705_0 = (0);
    RogueInt32 _auto_706_0 = (((_auto_704_0->count) - (1)));
    for (;ROGUE_COND(((_auto_705_0) <= (_auto_706_0)));++_auto_705_0)
    {
      ROGUE_GC_CHECK;
      ROGUE_DEF_LOCAL_REF(RogueString*,element_0,(((RogueString*)(_auto_704_0->data->as_objects[_auto_705_0]))));
      RogueString_List__add__String( result_1, ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM4( 13, map_fn_0, element_0 ))) );
    }
  }
  return (RogueString_List*)(result_1);
}

RogueString* RogueArray_String___type_name( RogueArray* THIS )
{
  return (RogueString*)(Rogue_literal_strings[184]);
}

RogueInt32 RogueReal64__decimal_digit_count( RogueReal64 THIS )
{
  ROGUE_GC_CHECK;
  RogueStringBuilder__print__Real64( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__clear( ((RogueStringBuilder*)((RogueClassPrimitiveWorkBuffer*)ROGUE_SINGLETON(PrimitiveWorkBuffer))) )))), ROGUE_ARG(THIS) );
  RogueOptionalInt32 dot_0 = (((RogueStringBuilder__locate__Character( ((RogueStringBuilder*)((RogueClassPrimitiveWorkBuffer*)ROGUE_SINGLETON(PrimitiveWorkBuffer))), (RogueCharacter)'.' ))));
  if (ROGUE_COND(!(dot_0.exists)))
  {
    return (RogueInt32)(0);
  }
  RogueInt32 start_1 = (((dot_0.value) + (1)));
  RogueInt32 count_2 = (((RogueClassPrimitiveWorkBuffer*)ROGUE_SINGLETON(PrimitiveWorkBuffer))->count);
  if (ROGUE_COND(((((count_2) == (((start_1) + (1))))) && (((((RogueStringBuilder__get__Int32( ((RogueStringBuilder*)((RogueClassPrimitiveWorkBuffer*)ROGUE_SINGLETON(PrimitiveWorkBuffer))), start_1 )))) == ((RogueCharacter)'0'))))))
  {
    return (RogueInt32)(0);
  }
  return (RogueInt32)(((count_2) - (start_1)));
}

RogueString* RogueReal64__format__OptionalInt32( RogueReal64 THIS, RogueOptionalInt32 decimal_digits_0 )
{
  ROGUE_GC_CHECK;
  RogueOptionalInt32 _auto_68_1 = (decimal_digits_0);
  RogueInt32 digits_2 = (((((_auto_68_1.exists))) ? (_auto_68_1.value) : ((RogueReal64__decimal_digit_count( ROGUE_ARG(THIS) )))));
  return (RogueString*)(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__Real64_Int32( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), ROGUE_ARG(THIS), digits_2 )))) ))));
}

RogueReal64 RogueReal64__fractional_part( RogueReal64 THIS )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((THIS) >= (0.0))))
  {
    return (RogueReal64)(((THIS) - (((RogueReal64__whole_part( ROGUE_ARG(THIS) ))))));
  }
  else
  {
    return (RogueReal64)(((((RogueReal64__whole_part( ROGUE_ARG(THIS) )))) - (THIS)));
  }
}

RogueLogical RogueReal64__is_infinite( RogueReal64 THIS )
{
  ROGUE_GC_CHECK;
  return (RogueLogical)(((((THIS) == (THIS))) && (((((THIS) - (THIS))) != (0.0)))));
}

RogueLogical RogueReal64__is_not_a_number( RogueReal64 THIS )
{
  ROGUE_GC_CHECK;
  return (RogueLogical)(((THIS) != (THIS)));
}

RogueReal64 RogueReal64__whole_part( RogueReal64 THIS )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((THIS) >= (0.0))))
  {
    return (RogueReal64)(floor((double)THIS));
  }
  else
  {
    return (RogueReal64)(ceil((double)THIS));
  }
}

RogueStringBuilder* RogueInt64__print_in_power2_base__Int32_Int32_StringBuilder( RogueInt64 THIS, RogueInt32 base_0, RogueInt32 digits_1, RogueStringBuilder* buffer_2 )
{
  ROGUE_GC_CHECK;
  RogueInt32 bits_3 = (0);
  RogueInt32 temp_4 = (base_0);
  while (ROGUE_COND(((temp_4) > (1))))
  {
    ROGUE_GC_CHECK;
    ++bits_3;
    temp_4 = ((RogueInt32)(((temp_4) >> (1))));
  }
  if (ROGUE_COND(((digits_1) > (1))))
  {
    RogueInt64__print_in_power2_base__Int32_Int32_StringBuilder( ROGUE_ARG(((THIS) >> (((RogueInt64)(bits_3))))), base_0, ROGUE_ARG(((digits_1) - (1))), buffer_2 );
  }
  RogueStringBuilder__print__Character_Logical( buffer_2, ROGUE_ARG(((RogueInt32__to_digit( ROGUE_ARG(((RogueInt32)(((THIS) & (((RogueInt64)(((base_0) - (1))))))))) )))), true );
  return (RogueStringBuilder*)(buffer_2);
}

RogueString* RogueInt64__to_hex_string__Int32( RogueInt64 THIS, RogueInt32 digits_0 )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueInt64__print_in_power2_base__Int32_Int32_StringBuilder( ROGUE_ARG(THIS), 16, digits_0, ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))) )))) ))));
}

RogueLogical RogueCharacter__is_identifier__Logical_Logical( RogueCharacter THIS, RogueLogical start_0, RogueLogical allow_dollar_1 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((((((((THIS) >= ((RogueCharacter)'a'))) && (((THIS) <= ((RogueCharacter)'z'))))) || (((((THIS) >= ((RogueCharacter)'A'))) && (((THIS) <= ((RogueCharacter)'Z'))))))) || (((THIS) == ((RogueCharacter)'_'))))))
  {
    return (RogueLogical)(true);
  }
  if (ROGUE_COND(((!(start_0)) && (((((THIS) >= ((RogueCharacter)'0'))) && (((THIS) <= ((RogueCharacter)'9'))))))))
  {
    return (RogueLogical)(true);
  }
  if (ROGUE_COND(((allow_dollar_1) && (((THIS) == ((RogueCharacter)'$'))))))
  {
    return (RogueLogical)(true);
  }
  return (RogueLogical)(false);
}

RogueLogical RogueCharacter__is_letter( RogueCharacter THIS )
{
  ROGUE_GC_CHECK;
  return (RogueLogical)(((((((THIS) >= ((RogueCharacter)'a'))) && (((THIS) <= ((RogueCharacter)'z'))))) || (((((THIS) >= ((RogueCharacter)'A'))) && (((THIS) <= ((RogueCharacter)'Z')))))));
}

RogueString* RogueCharacter__to_String( RogueCharacter THIS )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(((RogueString*)(RogueString__operatorPLUS__Character( ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], Rogue_literal_strings[0] ))), ROGUE_ARG(THIS) ))));
}

RogueInt32 RogueCharacter__to_number__Int32( RogueCharacter THIS, RogueInt32 base_0 )
{
  ROGUE_GC_CHECK;
  RogueInt32 value_1 = 0;
  if (ROGUE_COND(((((THIS) >= ((RogueCharacter)'0'))) && (((THIS) <= ((RogueCharacter)'9'))))))
  {
    value_1 = ((RogueInt32)(((RogueInt32)(((THIS) - ((RogueCharacter)'0'))))));
  }
  else if (ROGUE_COND(((((THIS) >= ((RogueCharacter)'A'))) && (((THIS) <= ((RogueCharacter)'Z'))))))
  {
    value_1 = ((RogueInt32)(((10) + (((RogueInt32)(((THIS) - ((RogueCharacter)'A'))))))));
  }
  else if (ROGUE_COND(((((THIS) >= ((RogueCharacter)'a'))) && (((THIS) <= ((RogueCharacter)'z'))))))
  {
    value_1 = ((RogueInt32)(((10) + (((RogueInt32)(((THIS) - ((RogueCharacter)'a'))))))));
  }
  else
  {
    return (RogueInt32)(-1);
  }
  if (ROGUE_COND(((value_1) < (base_0))))
  {
    return (RogueInt32)(value_1);
  }
  else
  {
    return (RogueInt32)(-1);
  }
}

RogueCharacter_List* RogueCharacter_List__init_object( RogueCharacter_List* THIS )
{
  ROGUE_GC_CHECK;
  RogueGenericList__init_object( ROGUE_ARG(((RogueClassGenericList*)THIS)) );
  return (RogueCharacter_List*)(THIS);
}

RogueCharacter_List* RogueCharacter_List__init( RogueCharacter_List* THIS )
{
  ROGUE_GC_CHECK;
  RogueCharacter_List__init__Int32( ROGUE_ARG(THIS), 0 );
  return (RogueCharacter_List*)(THIS);
}

RogueString* RogueCharacter_List__to_String( RogueCharacter_List* THIS )
{
  ROGUE_GC_CHECK;
  ROGUE_DEF_LOCAL_REF(RogueStringBuilder*,buffer_0,(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))));
  RogueStringBuilder__print__Character_Logical( buffer_0, (RogueCharacter)'[', true );
  RogueLogical first_1 = (true);
  {
    ROGUE_DEF_LOCAL_REF(RogueCharacter_List*,_auto_936_0,(THIS));
    RogueInt32 _auto_937_0 = (0);
    RogueInt32 _auto_938_0 = (((_auto_936_0->count) - (1)));
    for (;ROGUE_COND(((_auto_937_0) <= (_auto_938_0)));++_auto_937_0)
    {
      ROGUE_GC_CHECK;
      RogueCharacter value_0 = (_auto_936_0->data->as_characters[_auto_937_0]);
      if (ROGUE_COND(first_1))
      {
        first_1 = ((RogueLogical)(false));
      }
      else
      {
        RogueStringBuilder__print__Character_Logical( buffer_0, (RogueCharacter)',', true );
      }
      if (ROGUE_COND((false)))
      {
      }
      else
      {
        RogueStringBuilder__print__String( buffer_0, ROGUE_ARG(((RogueString*)(RogueCharacter__to_String( value_0 )))) );
      }
    }
  }
  RogueStringBuilder__print__Character_Logical( buffer_0, (RogueCharacter)']', true );
  return (RogueString*)(((RogueString*)(RogueStringBuilder__to_String( buffer_0 ))));
}

RogueString* RogueCharacter_List__type_name( RogueCharacter_List* THIS )
{
  return (RogueString*)(Rogue_literal_strings[195]);
}

RogueCharacter_List* RogueCharacter_List__init__Int32( RogueCharacter_List* THIS, RogueInt32 initial_capacity_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(!!(initial_capacity_0)))
  {
    THIS->data = RogueType_create_array( initial_capacity_0, sizeof(RogueCharacter), false, 31 );
  }
  return (RogueCharacter_List*)(THIS);
}

RogueCharacter_List* RogueCharacter_List__add__Character( RogueCharacter_List* THIS, RogueCharacter value_0 )
{
  ROGUE_GC_CHECK;
  ((RogueCharacter_List*)(RogueCharacter_List__reserve__Int32( ROGUE_ARG(THIS), 1 )))->data->as_characters[THIS->count] = (value_0);
  THIS->count = ((THIS->count) + (1));
  return (RogueCharacter_List*)(THIS);
}

RogueInt32 RogueCharacter_List__capacity( RogueCharacter_List* THIS )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(!(!!(THIS->data))))
  {
    return (RogueInt32)(0);
  }
  return (RogueInt32)(THIS->data->count);
}

RogueCharacter_List* RogueCharacter_List__discard_from__Int32( RogueCharacter_List* THIS, RogueInt32 index_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(!!(THIS->data)))
  {
    RogueInt32 n_1 = (((THIS->count) - (index_0)));
    if (ROGUE_COND(((n_1) > (0))))
    {
      RogueArray__zero__Int32_Int32( ROGUE_ARG(((RogueArray*)THIS->data)), index_0, n_1 );
      THIS->count = index_0;
    }
  }
  return (RogueCharacter_List*)(THIS);
}

RogueCharacter_List* RogueCharacter_List__reserve__Int32( RogueCharacter_List* THIS, RogueInt32 additional_elements_0 )
{
  ROGUE_GC_CHECK;
  RogueInt32 required_capacity_1 = (((THIS->count) + (additional_elements_0)));
  if (ROGUE_COND(!(!!(THIS->data))))
  {
    if (ROGUE_COND(((required_capacity_1) < (10))))
    {
      required_capacity_1 = ((RogueInt32)(10));
    }
    THIS->data = RogueType_create_array( required_capacity_1, sizeof(RogueCharacter), false, 31 );
  }
  else if (ROGUE_COND(((required_capacity_1) > (THIS->data->count))))
  {
    RogueInt32 cap_2 = (((RogueCharacter_List__capacity( ROGUE_ARG(THIS) ))));
    if (ROGUE_COND(((required_capacity_1) < (((cap_2) + (cap_2))))))
    {
      required_capacity_1 = ((RogueInt32)(((cap_2) + (cap_2))));
    }
    ROGUE_DEF_LOCAL_REF(RogueArray*,new_data_3,(RogueType_create_array( required_capacity_1, sizeof(RogueCharacter), false, 31 )));
    RogueArray_set(new_data_3,0,((RogueArray*)(THIS->data)),0,-1);
    THIS->data = new_data_3;
  }
  return (RogueCharacter_List*)(THIS);
}

RogueClassListRewriter_Character_* RogueCharacter_List__rewriter( RogueCharacter_List* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueClassListRewriter_Character_*)(((RogueClassListRewriter_Character_*)(RogueListRewriter_Character___init__Character_List( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassListRewriter_Character_*,ROGUE_CREATE_OBJECT(ListRewriter_Character_))), ROGUE_ARG(THIS) ))));
}

RogueString* RogueArray_Character___type_name( RogueArray* THIS )
{
  return (RogueString*)(Rogue_literal_strings[189]);
}

RogueClassValueList* RogueValueList__init_object( RogueClassValueList* THIS )
{
  ROGUE_GC_CHECK;
  RogueValue__init_object( ROGUE_ARG(((RogueClassValue*)THIS)) );
  return (RogueClassValueList*)(THIS);
}

RogueClassValueList* RogueValueList__init( RogueClassValueList* THIS )
{
  ROGUE_GC_CHECK;
  RogueValueList__init__Int32( ROGUE_ARG(THIS), 0 );
  return (RogueClassValueList*)(THIS);
}

RogueString* RogueValueList__to_String( RogueClassValueList* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(((RogueString*)(RogueValue_List__to_String( ROGUE_ARG(THIS->data) ))));
}

RogueString* RogueValueList__type_name( RogueClassValueList* THIS )
{
  return (RogueString*)(Rogue_literal_strings[166]);
}

RogueClassValueList* RogueValueList__add__Value( RogueClassValueList* THIS, RogueClassValue* value_0 )
{
  ROGUE_GC_CHECK;
  RogueValue_List__add__Value( ROGUE_ARG(THIS->data), value_0 );
  return (RogueClassValueList*)(THIS);
}

RogueLogical RogueValueList__contains__String( RogueClassValueList* THIS, RogueString* value_0 )
{
  ROGUE_GC_CHECK;
  {
    ROGUE_DEF_LOCAL_REF(RogueValue_List*,_auto_1024_0,(THIS->data));
    RogueInt32 _auto_1025_0 = (0);
    RogueInt32 _auto_1026_0 = (((_auto_1024_0->count) - (1)));
    for (;ROGUE_COND(((_auto_1025_0) <= (_auto_1026_0)));++_auto_1025_0)
    {
      ROGUE_GC_CHECK;
      ROGUE_DEF_LOCAL_REF(RogueClassValue*,existing_0,(((RogueClassValue*)(_auto_1024_0->data->as_objects[_auto_1025_0]))));
      if (ROGUE_COND((((((RogueOptionalValue__operator__Value( existing_0 ))) && ((Rogue_call_ROGUEM6( 49, existing_0 ))))) && ((RogueString__operatorEQUALSEQUALS__String_String( ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ((RogueObject*)existing_0) ))), value_0 ))))))
      {
        return (RogueLogical)(true);
      }
    }
  }
  return (RogueLogical)(false);
}

RogueLogical RogueValueList__contains__Value( RogueClassValueList* THIS, RogueClassValue* value_0 )
{
  ROGUE_GC_CHECK;
  {
    ROGUE_DEF_LOCAL_REF(RogueValue_List*,_auto_1027_0,(THIS->data));
    RogueInt32 _auto_1028_0 = (0);
    RogueInt32 _auto_1029_0 = (((_auto_1027_0->count) - (1)));
    for (;ROGUE_COND(((_auto_1028_0) <= (_auto_1029_0)));++_auto_1028_0)
    {
      ROGUE_GC_CHECK;
      ROGUE_DEF_LOCAL_REF(RogueClassValue*,existing_0,(((RogueClassValue*)(_auto_1027_0->data->as_objects[_auto_1028_0]))));
      if (ROGUE_COND((((RogueOptionalValue__operator__Value( existing_0 ))) && ((Rogue_call_ROGUEM2( 59, existing_0, value_0 ))))))
      {
        return (RogueLogical)(true);
      }
    }
  }
  return (RogueLogical)(false);
}

RogueInt32 RogueValueList__count( RogueClassValueList* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueInt32)(THIS->data->count);
}

RogueClassValue* RogueValueList__get__Int32( RogueClassValueList* THIS, RogueInt32 index_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((((index_0) < (0))) || (((index_0) >= (THIS->data->count))))))
  {
    return (RogueClassValue*)(((RogueClassValue*)(((RogueClassUndefinedValue*)ROGUE_SINGLETON(UndefinedValue)))));
  }
  ROGUE_DEF_LOCAL_REF(RogueClassValue*,result_1,(((RogueClassValue*)(THIS->data->data->as_objects[index_0]))));
  if (ROGUE_COND(((void*)result_1) == ((void*)NULL)))
  {
    return (RogueClassValue*)(((RogueClassValue*)(((RogueClassUndefinedValue*)ROGUE_SINGLETON(UndefinedValue)))));
  }
  return (RogueClassValue*)(result_1);
}

RogueLogical RogueValueList__is_collection( RogueClassValueList* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueLogical)(true);
}

RogueClassValue* RogueValueList__remove__Value( RogueClassValueList* THIS, RogueClassValue* value_0 )
{
  ROGUE_GC_CHECK;
  return (RogueClassValue*)(((RogueClassValue*)(RogueValue_List__remove__Value( ROGUE_ARG(THIS->data), value_0 ))));
}

RogueLogical RogueValueList__to_Logical( RogueClassValueList* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueLogical)(true);
}

RogueStringBuilder* RogueValueList__to_json__StringBuilder_Int32( RogueClassValueList* THIS, RogueStringBuilder* buffer_0, RogueInt32 flags_1 )
{
  ROGUE_GC_CHECK;
  RogueLogical pretty_print_2 = (((!!(((flags_1) & (1)))) && (((((RogueValue__is_complex( ROGUE_ARG(((RogueClassValue*)THIS)) )))) || (!!(((flags_1) & (2))))))));
  RogueStringBuilder__print__Character_Logical( buffer_0, (RogueCharacter)'[', true );
  if (ROGUE_COND(pretty_print_2))
  {
    RogueStringBuilder__println( buffer_0 );
    buffer_0->indent += 2;
  }
  RogueLogical first_3 = (true);
  {
    ROGUE_DEF_LOCAL_REF(RogueValue_List*,_auto_1056_0,(THIS->data));
    RogueInt32 _auto_1057_0 = (0);
    RogueInt32 _auto_1058_0 = (((_auto_1056_0->count) - (1)));
    for (;ROGUE_COND(((_auto_1057_0) <= (_auto_1058_0)));++_auto_1057_0)
    {
      ROGUE_GC_CHECK;
      ROGUE_DEF_LOCAL_REF(RogueClassValue*,value_0,(((RogueClassValue*)(_auto_1056_0->data->as_objects[_auto_1057_0]))));
      if (ROGUE_COND(first_3))
      {
        first_3 = ((RogueLogical)(false));
      }
      else
      {
        if (ROGUE_COND(!(!!(((flags_1) & (2))))))
        {
          RogueStringBuilder__print__Character_Logical( buffer_0, (RogueCharacter)',', true );
        }
        if (ROGUE_COND(pretty_print_2))
        {
          RogueStringBuilder__println( buffer_0 );
        }
      }
      if (ROGUE_COND(((((void*)value_0) != ((void*)NULL)) && ((((RogueOptionalValue__operator__Value( value_0 ))) || ((Rogue_call_ROGUEM6( 43, value_0 ))))))))
      {
        Rogue_call_ROGUEM10( 115, value_0, buffer_0, flags_1 );
      }
      else
      {
        RogueStringBuilder__print__String( buffer_0, Rogue_literal_strings[1] );
      }
    }
  }
  if (ROGUE_COND(pretty_print_2))
  {
    RogueStringBuilder__println( buffer_0 );
    buffer_0->indent -= 2;
  }
  RogueStringBuilder__print__Character_Logical( buffer_0, (RogueCharacter)']', true );
  return (RogueStringBuilder*)(buffer_0);
}

RogueClassValueList* RogueValueList__init__Int32( RogueClassValueList* THIS, RogueInt32 initial_capacity_0 )
{
  ROGUE_GC_CHECK;
  THIS->data = ((RogueValue_List*)(RogueValue_List__init__Int32( ROGUE_ARG(ROGUE_CREATE_REF(RogueValue_List*,ROGUE_CREATE_OBJECT(Value_List))), initial_capacity_0 )));
  return (RogueClassValueList*)(THIS);
}

RogueValue_List* RogueValue_List__init_object( RogueValue_List* THIS )
{
  ROGUE_GC_CHECK;
  RogueGenericList__init_object( ROGUE_ARG(((RogueClassGenericList*)THIS)) );
  return (RogueValue_List*)(THIS);
}

RogueValue_List* RogueValue_List__init( RogueValue_List* THIS )
{
  ROGUE_GC_CHECK;
  RogueValue_List__init__Int32( ROGUE_ARG(THIS), 0 );
  return (RogueValue_List*)(THIS);
}

RogueString* RogueValue_List__to_String( RogueValue_List* THIS )
{
  ROGUE_GC_CHECK;
  ROGUE_DEF_LOCAL_REF(RogueStringBuilder*,buffer_0,(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))));
  RogueStringBuilder__print__Character_Logical( buffer_0, (RogueCharacter)'[', true );
  RogueLogical first_1 = (true);
  {
    ROGUE_DEF_LOCAL_REF(RogueValue_List*,_auto_1059_0,(THIS));
    RogueInt32 _auto_1060_0 = (0);
    RogueInt32 _auto_1061_0 = (((_auto_1059_0->count) - (1)));
    for (;ROGUE_COND(((_auto_1060_0) <= (_auto_1061_0)));++_auto_1060_0)
    {
      ROGUE_GC_CHECK;
      ROGUE_DEF_LOCAL_REF(RogueClassValue*,value_0,(((RogueClassValue*)(_auto_1059_0->data->as_objects[_auto_1060_0]))));
      if (ROGUE_COND(first_1))
      {
        first_1 = ((RogueLogical)(false));
      }
      else
      {
        RogueStringBuilder__print__Character_Logical( buffer_0, (RogueCharacter)',', true );
      }
      if (ROGUE_COND(((void*)value_0) == ((void*)NULL)))
      {
        RogueStringBuilder__print__String( buffer_0, Rogue_literal_strings[1] );
      }
      else
      {
        RogueStringBuilder__print__String( buffer_0, ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ((RogueObject*)value_0) ))) );
      }
    }
  }
  RogueStringBuilder__print__Character_Logical( buffer_0, (RogueCharacter)']', true );
  return (RogueString*)(((RogueString*)(RogueStringBuilder__to_String( buffer_0 ))));
}

RogueString* RogueValue_List__type_name( RogueValue_List* THIS )
{
  return (RogueString*)(Rogue_literal_strings[196]);
}

RogueValue_List* RogueValue_List__init__Int32( RogueValue_List* THIS, RogueInt32 initial_capacity_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(!!(initial_capacity_0)))
  {
    THIS->data = RogueType_create_array( initial_capacity_0, sizeof(RogueClassValue*), true, 5 );
  }
  return (RogueValue_List*)(THIS);
}

RogueValue_List* RogueValue_List__add__Value( RogueValue_List* THIS, RogueClassValue* value_0 )
{
  ROGUE_GC_CHECK;
  ((RogueValue_List*)(RogueValue_List__reserve__Int32( ROGUE_ARG(THIS), 1 )))->data->as_objects[THIS->count] = value_0;
  THIS->count = ((THIS->count) + (1));
  return (RogueValue_List*)(THIS);
}

RogueInt32 RogueValue_List__capacity( RogueValue_List* THIS )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(!(!!(THIS->data))))
  {
    return (RogueInt32)(0);
  }
  return (RogueInt32)(THIS->data->count);
}

RogueOptionalInt32 RogueValue_List__locate__Value( RogueValue_List* THIS, RogueClassValue* value_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(true))
  {
    if (ROGUE_COND(((void*)value_0) == ((void*)NULL)))
    {
      {
        ROGUE_DEF_LOCAL_REF(RogueValue_List*,_auto_1103_0,(THIS));
        RogueInt32 i_0 = (0);
        RogueInt32 _auto_1104_0 = (((_auto_1103_0->count) - (1)));
        for (;ROGUE_COND(((i_0) <= (_auto_1104_0)));++i_0)
        {
          ROGUE_GC_CHECK;
          if (ROGUE_COND(((void*)value_0) == ((void*)((RogueClassValue*)(THIS->data->as_objects[i_0])))))
          {
            return (RogueOptionalInt32)(RogueOptionalInt32( i_0, true ));
          }
        }
      }
      return (RogueOptionalInt32)((RogueOptionalInt32__create()));
    }
  }
  {
    ROGUE_DEF_LOCAL_REF(RogueValue_List*,_auto_1105_0,(THIS));
    RogueInt32 i_0 = (0);
    RogueInt32 _auto_1106_0 = (((_auto_1105_0->count) - (1)));
    for (;ROGUE_COND(((i_0) <= (_auto_1106_0)));++i_0)
    {
      ROGUE_GC_CHECK;
      if (ROGUE_COND((Rogue_call_ROGUEM2( 59, value_0, ROGUE_ARG(((RogueClassValue*)(THIS->data->as_objects[i_0]))) ))))
      {
        return (RogueOptionalInt32)(RogueOptionalInt32( i_0, true ));
      }
    }
  }
  return (RogueOptionalInt32)((RogueOptionalInt32__create()));
}

RogueValue_List* RogueValue_List__reserve__Int32( RogueValue_List* THIS, RogueInt32 additional_elements_0 )
{
  ROGUE_GC_CHECK;
  RogueInt32 required_capacity_1 = (((THIS->count) + (additional_elements_0)));
  if (ROGUE_COND(!(!!(THIS->data))))
  {
    if (ROGUE_COND(((required_capacity_1) < (10))))
    {
      required_capacity_1 = ((RogueInt32)(10));
    }
    THIS->data = RogueType_create_array( required_capacity_1, sizeof(RogueClassValue*), true, 5 );
  }
  else if (ROGUE_COND(((required_capacity_1) > (THIS->data->count))))
  {
    RogueInt32 cap_2 = (((RogueValue_List__capacity( ROGUE_ARG(THIS) ))));
    if (ROGUE_COND(((required_capacity_1) < (((cap_2) + (cap_2))))))
    {
      required_capacity_1 = ((RogueInt32)(((cap_2) + (cap_2))));
    }
    ROGUE_DEF_LOCAL_REF(RogueArray*,new_data_3,(RogueType_create_array( required_capacity_1, sizeof(RogueClassValue*), true, 5 )));
    RogueArray_set(new_data_3,0,((RogueArray*)(THIS->data)),0,-1);
    THIS->data = new_data_3;
  }
  return (RogueValue_List*)(THIS);
}

RogueClassValue* RogueValue_List__remove__Value( RogueValue_List* THIS, RogueClassValue* value_0 )
{
  ROGUE_GC_CHECK;
  RogueOptionalInt32 index_1 = (((RogueValue_List__locate__Value( ROGUE_ARG(THIS), value_0 ))));
  if (ROGUE_COND(index_1.exists))
  {
    return (RogueClassValue*)(((RogueClassValue*)(RogueValue_List__remove_at__Int32( ROGUE_ARG(THIS), ROGUE_ARG(index_1.value) ))));
  }
  else
  {
    return (RogueClassValue*)(value_0);
  }
}

RogueClassValue* RogueValue_List__remove_at__Int32( RogueValue_List* THIS, RogueInt32 index_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((RogueLogical)((((unsigned int)index_0) >= (unsigned int)THIS->count)))))
  {
    throw ((RogueClassOutOfBoundsError*)(RogueOutOfBoundsError___throw( ROGUE_ARG(((RogueClassOutOfBoundsError*)(RogueOutOfBoundsError__init__Int32_Int32( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassOutOfBoundsError*,ROGUE_CREATE_OBJECT(OutOfBoundsError))), index_0, ROGUE_ARG(THIS->count) )))) )));
  }
  ROGUE_DEF_LOCAL_REF(RogueClassValue*,result_1,(((RogueClassValue*)(THIS->data->as_objects[index_0]))));
  RogueArray_set(THIS->data,index_0,((RogueArray*)(THIS->data)),((index_0) + (1)),-1);
  ROGUE_DEF_LOCAL_REF(RogueClassValue*,zero_value_2,0);
  THIS->count = ((THIS->count) + (-1));
  THIS->data->as_objects[THIS->count] = zero_value_2;
  return (RogueClassValue*)(result_1);
}

RogueString* RogueArray_Value___type_name( RogueArray* THIS )
{
  return (RogueString*)(Rogue_literal_strings[190]);
}

RogueClassStringConsolidationTable* RogueStringConsolidationTable__init_object( RogueClassStringConsolidationTable* THIS )
{
  ROGUE_GC_CHECK;
  RogueStringTable_String___init_object( ROGUE_ARG(((RogueClassStringTable_String_*)THIS)) );
  return (RogueClassStringConsolidationTable*)(THIS);
}

RogueString* RogueStringConsolidationTable__type_name( RogueClassStringConsolidationTable* THIS )
{
  return (RogueString*)(Rogue_literal_strings[209]);
}

RogueString* RogueStringConsolidationTable__get__String( RogueClassStringConsolidationTable* THIS, RogueString* st_0 )
{
  ROGUE_GC_CHECK;
  ROGUE_DEF_LOCAL_REF(RogueString*,result_1,((RogueTable_String_String___get__String( ROGUE_ARG(((RogueClassTable_String_String_*)THIS)), st_0 ))));
  if (ROGUE_COND(!!(result_1)))
  {
    return (RogueString*)(result_1);
  }
  RogueTable_String_String___set__String_String( ROGUE_ARG(((RogueClassTable_String_String_*)THIS)), st_0, st_0 );
  return (RogueString*)(st_0);
}

RogueClassStringTable_String_* RogueStringTable_String___init_object( RogueClassStringTable_String_* THIS )
{
  ROGUE_GC_CHECK;
  RogueTable_String_String___init_object( ROGUE_ARG(((RogueClassTable_String_String_*)THIS)) );
  return (RogueClassStringTable_String_*)(THIS);
}

RogueString* RogueStringTable_String___type_name( RogueClassStringTable_String_* THIS )
{
  return (RogueString*)(Rogue_literal_strings[203]);
}

RogueClassTable_String_String_* RogueTable_String_String___init_object( RogueClassTable_String_String_* THIS )
{
  ROGUE_GC_CHECK;
  RogueObject__init_object( ROGUE_ARG(((RogueObject*)THIS)) );
  return (RogueClassTable_String_String_*)(THIS);
}

RogueClassTable_String_String_* RogueTable_String_String___init( RogueClassTable_String_String_* THIS )
{
  ROGUE_GC_CHECK;
  RogueTable_String_String___init__Int32( ROGUE_ARG(THIS), 16 );
  return (RogueClassTable_String_String_*)(THIS);
}

RogueString* RogueTable_String_String___to_String( RogueClassTable_String_String_* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueTable_String_String___print_to__StringBuilder( ROGUE_ARG(THIS), ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))) )))) ))));
}

RogueString* RogueTable_String_String___type_name( RogueClassTable_String_String_* THIS )
{
  return (RogueString*)(Rogue_literal_strings[140]);
}

RogueClassTable_String_String_* RogueTable_String_String___init__Int32( RogueClassTable_String_String_* THIS, RogueInt32 bin_count_0 )
{
  ROGUE_GC_CHECK;
  RogueInt32 bins_power_of_2_1 = (1);
  while (ROGUE_COND(((bins_power_of_2_1) < (bin_count_0))))
  {
    ROGUE_GC_CHECK;
    bins_power_of_2_1 = ((RogueInt32)(((bins_power_of_2_1) << (1))));
  }
  bin_count_0 = ((RogueInt32)(bins_power_of_2_1));
  THIS->bin_mask = ((bin_count_0) - (1));
  THIS->bins = RogueType_create_array( bin_count_0, sizeof(RogueClassTableEntry_String_String_*), true, 41 );
  return (RogueClassTable_String_String_*)(THIS);
}

RogueClassTableEntry_String_String_* RogueTable_String_String___find__String( RogueClassTable_String_String_* THIS, RogueString* key_0 )
{
  ROGUE_GC_CHECK;
  RogueInt32 hash_1 = (((RogueString__hash_code( key_0 ))));
  ROGUE_DEF_LOCAL_REF(RogueClassTableEntry_String_String_*,entry_2,(((RogueClassTableEntry_String_String_*)(THIS->bins->as_objects[((hash_1) & (THIS->bin_mask))]))));
  while (ROGUE_COND(!!(entry_2)))
  {
    ROGUE_GC_CHECK;
    if (ROGUE_COND(((((entry_2->hash) == (hash_1))) && ((RogueString__operatorEQUALSEQUALS__String_String( ROGUE_ARG(entry_2->key), key_0 ))))))
    {
      return (RogueClassTableEntry_String_String_*)(entry_2);
    }
    entry_2 = ((RogueClassTableEntry_String_String_*)(entry_2->adjacent_entry));
  }
  return (RogueClassTableEntry_String_String_*)(((RogueClassTableEntry_String_String_*)(NULL)));
}

RogueString* RogueTable_String_String___get__String( RogueClassTable_String_String_* THIS, RogueString* key_0 )
{
  ROGUE_GC_CHECK;
  ROGUE_DEF_LOCAL_REF(RogueClassTableEntry_String_String_*,entry_1,(((RogueClassTableEntry_String_String_*)(RogueTable_String_String___find__String( ROGUE_ARG(THIS), key_0 )))));
  if (ROGUE_COND(!!(entry_1)))
  {
    return (RogueString*)(entry_1->value);
  }
  else
  {
    ROGUE_DEF_LOCAL_REF(RogueString*,default_value_2,0);
    return (RogueString*)(default_value_2);
  }
}

RogueStringBuilder* RogueTable_String_String___print_to__StringBuilder( RogueClassTable_String_String_* THIS, RogueStringBuilder* buffer_0 )
{
  ROGUE_GC_CHECK;
  RogueStringBuilder__print__Character_Logical( buffer_0, (RogueCharacter)'{', true );
  ROGUE_DEF_LOCAL_REF(RogueClassTableEntry_String_String_*,cur_1,(THIS->first_entry));
  RogueInt32 i_2 = (0);
  while (ROGUE_COND(!!(cur_1)))
  {
    ROGUE_GC_CHECK;
    if (ROGUE_COND(((i_2) > (0))))
    {
      RogueStringBuilder__print__Character_Logical( buffer_0, (RogueCharacter)',', true );
    }
    RogueStringBuilder__print__String( buffer_0, ROGUE_ARG(cur_1->key) );
    RogueStringBuilder__print__Character_Logical( buffer_0, (RogueCharacter)':', true );
    RogueStringBuilder__print__String( buffer_0, ROGUE_ARG(cur_1->value) );
    cur_1 = ((RogueClassTableEntry_String_String_*)(cur_1->next_entry));
    ++i_2;
  }
  RogueStringBuilder__print__Character_Logical( buffer_0, (RogueCharacter)'}', true );
  return (RogueStringBuilder*)(buffer_0);
}

RogueClassTable_String_String_* RogueTable_String_String___set__String_String( RogueClassTable_String_String_* THIS, RogueString* key_0, RogueString* value_1 )
{
  ROGUE_GC_CHECK;
  ROGUE_DEF_LOCAL_REF(RogueClassTableEntry_String_String_*,entry_2,(((RogueClassTableEntry_String_String_*)(RogueTable_String_String___find__String( ROGUE_ARG(THIS), key_0 )))));
  if (ROGUE_COND(!!(entry_2)))
  {
    entry_2->value = value_1;
    if (ROGUE_COND(!!(THIS->sort_function)))
    {
      RogueTable_String_String____adjust_entry_order__TableEntry_String_String_( ROGUE_ARG(THIS), entry_2 );
    }
    return (RogueClassTable_String_String_*)(THIS);
  }
  if (ROGUE_COND(((THIS->count) >= (THIS->bins->count))))
  {
    RogueTable_String_String____grow( ROGUE_ARG(THIS) );
  }
  RogueInt32 hash_3 = (((RogueString__hash_code( key_0 ))));
  RogueInt32 index_4 = (((hash_3) & (THIS->bin_mask)));
  if (ROGUE_COND(!(!!(entry_2))))
  {
    entry_2 = ((RogueClassTableEntry_String_String_*)(((RogueClassTableEntry_String_String_*)(RogueTableEntry_String_String___init__String_String_Int32( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassTableEntry_String_String_*,ROGUE_CREATE_OBJECT(TableEntry_String_String_))), key_0, value_1, hash_3 )))));
  }
  entry_2->adjacent_entry = ((RogueClassTableEntry_String_String_*)(THIS->bins->as_objects[index_4]));
  THIS->bins->as_objects[index_4] = entry_2;
  RogueTable_String_String____place_entry_in_order__TableEntry_String_String_( ROGUE_ARG(THIS), entry_2 );
  THIS->count = ((THIS->count) + (1));
  return (RogueClassTable_String_String_*)(THIS);
}

void RogueTable_String_String____adjust_entry_order__TableEntry_String_String_( RogueClassTable_String_String_* THIS, RogueClassTableEntry_String_String_* entry_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((void*)THIS->first_entry) == ((void*)THIS->last_entry)))
  {
    return;
  }
  if (ROGUE_COND(((void*)entry_0) == ((void*)THIS->first_entry)))
  {
    if (ROGUE_COND(((Rogue_Function_TableEntry_String_String__TableEntry_String_String__RETURNSLogical___call__TableEntry_String_String__TableEntry_String_String_( ROGUE_ARG(THIS->sort_function), entry_0, ROGUE_ARG(entry_0->next_entry) )))))
    {
      return;
    }
  }
  else if (ROGUE_COND(((void*)entry_0) == ((void*)THIS->last_entry)))
  {
    if (ROGUE_COND(((Rogue_Function_TableEntry_String_String__TableEntry_String_String__RETURNSLogical___call__TableEntry_String_String__TableEntry_String_String_( ROGUE_ARG(THIS->sort_function), ROGUE_ARG(entry_0->previous_entry), entry_0 )))))
    {
      return;
    }
  }
  else if (ROGUE_COND(((((Rogue_Function_TableEntry_String_String__TableEntry_String_String__RETURNSLogical___call__TableEntry_String_String__TableEntry_String_String_( ROGUE_ARG(THIS->sort_function), ROGUE_ARG(entry_0->previous_entry), entry_0 )))) && (((Rogue_Function_TableEntry_String_String__TableEntry_String_String__RETURNSLogical___call__TableEntry_String_String__TableEntry_String_String_( ROGUE_ARG(THIS->sort_function), entry_0, ROGUE_ARG(entry_0->next_entry) )))))))
  {
    return;
  }
  RogueTable_String_String____unlink__TableEntry_String_String_( ROGUE_ARG(THIS), entry_0 );
  RogueTable_String_String____place_entry_in_order__TableEntry_String_String_( ROGUE_ARG(THIS), entry_0 );
}

void RogueTable_String_String____place_entry_in_order__TableEntry_String_String_( RogueClassTable_String_String_* THIS, RogueClassTableEntry_String_String_* entry_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(!!(THIS->first_entry)))
  {
    if (ROGUE_COND(!!(THIS->sort_function)))
    {
      if (ROGUE_COND(((Rogue_Function_TableEntry_String_String__TableEntry_String_String__RETURNSLogical___call__TableEntry_String_String__TableEntry_String_String_( ROGUE_ARG(THIS->sort_function), entry_0, ROGUE_ARG(THIS->first_entry) )))))
      {
        entry_0->next_entry = THIS->first_entry;
        THIS->first_entry->previous_entry = entry_0;
        THIS->first_entry = entry_0;
      }
      else if (ROGUE_COND(((Rogue_Function_TableEntry_String_String__TableEntry_String_String__RETURNSLogical___call__TableEntry_String_String__TableEntry_String_String_( ROGUE_ARG(THIS->sort_function), ROGUE_ARG(THIS->last_entry), entry_0 )))))
      {
        THIS->last_entry->next_entry = entry_0;
        entry_0->previous_entry = THIS->last_entry;
        THIS->last_entry = entry_0;
      }
      else
      {
        ROGUE_DEF_LOCAL_REF(RogueClassTableEntry_String_String_*,cur_1,(THIS->first_entry));
        while (ROGUE_COND(!!(cur_1->next_entry)))
        {
          ROGUE_GC_CHECK;
          if (ROGUE_COND(((Rogue_Function_TableEntry_String_String__TableEntry_String_String__RETURNSLogical___call__TableEntry_String_String__TableEntry_String_String_( ROGUE_ARG(THIS->sort_function), entry_0, ROGUE_ARG(cur_1->next_entry) )))))
          {
            entry_0->previous_entry = cur_1;
            entry_0->next_entry = cur_1->next_entry;
            entry_0->next_entry->previous_entry = entry_0;
            cur_1->next_entry = entry_0;
            goto _auto_1174;
          }
          cur_1 = ((RogueClassTableEntry_String_String_*)(cur_1->next_entry));
        }
        _auto_1174:;
      }
    }
    else
    {
      THIS->last_entry->next_entry = entry_0;
      entry_0->previous_entry = THIS->last_entry;
      THIS->last_entry = entry_0;
    }
  }
  else
  {
    THIS->first_entry = entry_0;
    THIS->last_entry = entry_0;
  }
}

void RogueTable_String_String____unlink__TableEntry_String_String_( RogueClassTable_String_String_* THIS, RogueClassTableEntry_String_String_* entry_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((void*)entry_0) == ((void*)THIS->first_entry)))
  {
    if (ROGUE_COND(((void*)entry_0) == ((void*)THIS->last_entry)))
    {
      THIS->first_entry = ((RogueClassTableEntry_String_String_*)(NULL));
      THIS->last_entry = ((RogueClassTableEntry_String_String_*)(NULL));
    }
    else
    {
      THIS->first_entry = entry_0->next_entry;
      THIS->first_entry->previous_entry = ((RogueClassTableEntry_String_String_*)(NULL));
    }
  }
  else if (ROGUE_COND(((void*)entry_0) == ((void*)THIS->last_entry)))
  {
    THIS->last_entry = entry_0->previous_entry;
    THIS->last_entry->next_entry = ((RogueClassTableEntry_String_String_*)(NULL));
  }
  else
  {
    entry_0->previous_entry->next_entry = entry_0->next_entry;
    entry_0->next_entry->previous_entry = entry_0->previous_entry;
  }
}

void RogueTable_String_String____grow( RogueClassTable_String_String_* THIS )
{
  ROGUE_GC_CHECK;
  THIS->bins = RogueType_create_array( ((THIS->bins->count) * (2)), sizeof(RogueClassTableEntry_String_String_*), true, 41 );
  THIS->bin_mask = ((((THIS->bin_mask) << (1))) | (1));
  ROGUE_DEF_LOCAL_REF(RogueClassTableEntry_String_String_*,cur_0,(THIS->first_entry));
  while (ROGUE_COND(!!(cur_0)))
  {
    ROGUE_GC_CHECK;
    RogueInt32 index_1 = (((cur_0->hash) & (THIS->bin_mask)));
    cur_0->adjacent_entry = ((RogueClassTableEntry_String_String_*)(THIS->bins->as_objects[index_1]));
    THIS->bins->as_objects[index_1] = cur_0;
    cur_0 = ((RogueClassTableEntry_String_String_*)(cur_0->next_entry));
  }
}

RogueString* RogueArray_TableEntry_String_String____type_name( RogueArray* THIS )
{
  return (RogueString*)(Rogue_literal_strings[191]);
}

RogueClassTableEntry_String_String_* RogueTableEntry_String_String___init_object( RogueClassTableEntry_String_String_* THIS )
{
  ROGUE_GC_CHECK;
  RogueObject__init_object( ROGUE_ARG(((RogueObject*)THIS)) );
  return (RogueClassTableEntry_String_String_*)(THIS);
}

RogueString* RogueTableEntry_String_String___to_String( RogueClassTableEntry_String_String_* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[8] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], ROGUE_ARG(THIS->key) ))) )))), Rogue_literal_strings[76] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], ROGUE_ARG(THIS->value) ))) )))), Rogue_literal_strings[11] )))) ))));
}

RogueString* RogueTableEntry_String_String___type_name( RogueClassTableEntry_String_String_* THIS )
{
  return (RogueString*)(Rogue_literal_strings[160]);
}

RogueClassTableEntry_String_String_* RogueTableEntry_String_String___init__String_String_Int32( RogueClassTableEntry_String_String_* THIS, RogueString* _key_0, RogueString* _value_1, RogueInt32 _hash_2 )
{
  ROGUE_GC_CHECK;
  THIS->key = _key_0;
  THIS->value = _value_1;
  THIS->hash = _hash_2;
  return (RogueClassTableEntry_String_String_*)(THIS);
}

RogueClass_Function_TableEntry_String_String__TableEntry_String_String__RETURNSLogical_* Rogue_Function_TableEntry_String_String__TableEntry_String_String__RETURNSLogical___init_object( RogueClass_Function_TableEntry_String_String__TableEntry_String_String__RETURNSLogical_* THIS )
{
  ROGUE_GC_CHECK;
  RogueObject__init_object( ROGUE_ARG(((RogueObject*)THIS)) );
  return (RogueClass_Function_TableEntry_String_String__TableEntry_String_String__RETURNSLogical_*)(THIS);
}

RogueString* Rogue_Function_TableEntry_String_String__TableEntry_String_String__RETURNSLogical___type_name( RogueClass_Function_TableEntry_String_String__TableEntry_String_String__RETURNSLogical_* THIS )
{
  return (RogueString*)(Rogue_literal_strings[161]);
}

RogueLogical Rogue_Function_TableEntry_String_String__TableEntry_String_String__RETURNSLogical___call__TableEntry_String_String__TableEntry_String_String_( RogueClass_Function_TableEntry_String_String__TableEntry_String_String__RETURNSLogical_* THIS, RogueClassTableEntry_String_String_* param1_0, RogueClassTableEntry_String_String_* param2_1 )
{
  return (RogueLogical)(false);
}

RogueClassReader_Character_* RogueReader_Character___close( RogueObject* THIS )
{
  switch (THIS->type->index)
  {
    case 45:
      return (RogueClassReader_Character_*)RogueConsole__close( (RogueClassConsole*)THIS );
    case 58:
      return (RogueClassReader_Character_*)RogueScanner__close( (RogueClassScanner*)THIS );
    case 77:
      return (RogueClassReader_Character_*)RogueUTF8Reader__close( (RogueClassUTF8Reader*)THIS );
    default:
      return 0;
  }
}

RogueLogical RogueReader_Character___has_another( RogueObject* THIS )
{
  switch (THIS->type->index)
  {
    case 45:
      return RogueConsole__has_another( (RogueClassConsole*)THIS );
    case 58:
      return RogueScanner__has_another( (RogueClassScanner*)THIS );
    case 77:
      return RogueUTF8Reader__has_another( (RogueClassUTF8Reader*)THIS );
    default:
      return 0;
  }
}

RogueCharacter RogueReader_Character___read( RogueObject* THIS )
{
  switch (THIS->type->index)
  {
    case 45:
      return RogueConsole__read( (RogueClassConsole*)THIS );
    case 58:
      return RogueScanner__read( (RogueClassScanner*)THIS );
    case 77:
      return RogueUTF8Reader__read( (RogueClassUTF8Reader*)THIS );
    default:
      return 0;
  }
}

RogueClassConsole* RogueConsole__init_object( RogueClassConsole* THIS )
{
  ROGUE_GC_CHECK;
  RogueObject__init_object( ROGUE_ARG(((RogueObject*)THIS)) );
  THIS->error = ((RogueClassConsoleErrorPrinter*)(((RogueObject*)Rogue_call_ROGUEM1( 1, ROGUE_ARG(((RogueObject*)ROGUE_CREATE_REF(RogueClassConsoleErrorPrinter*,ROGUE_CREATE_OBJECT(ConsoleErrorPrinter)))) ))));
  THIS->immediate_mode = false;
  THIS->is_blocking = true;
  THIS->decode_bytes = true;
  THIS->output_buffer = ((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )));
  THIS->input_buffer = ((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )));
  THIS->input_bytes = ((RogueByte_List*)(RogueByte_List__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueByte_List*,ROGUE_CREATE_OBJECT(Byte_List))) )));
  return (RogueClassConsole*)(THIS);
}

RogueClassConsole* RogueConsole__init( RogueClassConsole* THIS )
{
  ROGUE_GC_CHECK;
  tcgetattr( STDIN_FILENO, &THIS->original_terminal_settings );
  THIS->original_stdin_flags = fcntl( STDIN_FILENO, F_GETFL );

  RogueGlobal__on_exit___Function___( ((RogueClassGlobal*)ROGUE_SINGLETON(Global)), ROGUE_ARG(((RogueClass_Function___*)(((RogueClassFunction_1184*)(RogueFunction_1184__init__Console( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassFunction_1184*,ROGUE_CREATE_OBJECT(Function_1184))), ROGUE_ARG(THIS) )))))) );
  return (RogueClassConsole*)(THIS);
}

RogueString* RogueConsole__type_name( RogueClassConsole* THIS )
{
  return (RogueString*)(Rogue_literal_strings[142]);
}

RogueClassPrintWriter_output_buffer_* RogueConsole__close( RogueClassConsole* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueClassPrintWriter_output_buffer_*)(THIS);
}

RogueLogical RogueConsole__has_another( RogueClassConsole* THIS )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(!(THIS->next_input_character.exists)))
  {
    RogueConsole__fill_input_queue( ROGUE_ARG(THIS) );
    if (ROGUE_COND(!!(THIS->input_bytes->count)))
    {
      RogueByte b1_0 = (((RogueByte_List__remove_at__Int32( ROGUE_ARG(THIS->input_bytes), 0 ))));
      {
        {
          {
            if ( !(THIS->decode_bytes) ) goto _auto_1187;
            if ( !(THIS->input_bytes->count) ) goto _auto_1187;
            if (ROGUE_COND(((((RogueInt32)(b1_0))) == (27))))
            {
              if ( !(((((THIS->input_bytes->count) >= (2))) && (((((RogueInt32)(THIS->input_bytes->data->as_bytes[0]))) == (91))))) ) goto _auto_1187;
              RogueByte_List__remove_at__Int32( ROGUE_ARG(THIS->input_bytes), 0 );
              THIS->next_input_character = RogueOptionalInt32( ((((((RogueInt32)(((RogueByte_List__remove_at__Int32( ROGUE_ARG(THIS->input_bytes), 0 )))))) - (65))) + (17)), true );
            }
            else
            {
              if ( !(((((RogueInt32)(b1_0))) >= (192))) ) goto _auto_1187;
              RogueInt32 result_1 = 0;
              if (ROGUE_COND(((((((RogueInt32)(b1_0))) & (224))) == (192))))
              {
                if ( !(((THIS->input_bytes->count) >= (1))) ) goto _auto_1187;
                result_1 = ((RogueInt32)(((((RogueInt32)(b1_0))) & (31))));
                result_1 = ((RogueInt32)(((((result_1) << (6))) | (((((RogueInt32)(((RogueByte_List__remove_at__Int32( ROGUE_ARG(THIS->input_bytes), 0 )))))) & (63))))));
              }
              else if (ROGUE_COND(((((((RogueInt32)(b1_0))) & (240))) == (224))))
              {
                if ( !(((THIS->input_bytes->count) >= (2))) ) goto _auto_1187;
                result_1 = ((RogueInt32)(((((RogueInt32)(b1_0))) & (15))));
                result_1 = ((RogueInt32)(((((result_1) << (6))) | (((((RogueInt32)(((RogueByte_List__remove_at__Int32( ROGUE_ARG(THIS->input_bytes), 0 )))))) & (63))))));
                result_1 = ((RogueInt32)(((((result_1) << (6))) | (((((RogueInt32)(((RogueByte_List__remove_at__Int32( ROGUE_ARG(THIS->input_bytes), 0 )))))) & (63))))));
              }
              else if (ROGUE_COND(((((((RogueInt32)(b1_0))) & (248))) == (240))))
              {
                if ( !(((THIS->input_bytes->count) >= (3))) ) goto _auto_1187;
                result_1 = ((RogueInt32)(((((RogueInt32)(b1_0))) & (7))));
                result_1 = ((RogueInt32)(((((result_1) << (6))) | (((((RogueInt32)(((RogueByte_List__remove_at__Int32( ROGUE_ARG(THIS->input_bytes), 0 )))))) & (63))))));
                result_1 = ((RogueInt32)(((((result_1) << (6))) | (((((RogueInt32)(((RogueByte_List__remove_at__Int32( ROGUE_ARG(THIS->input_bytes), 0 )))))) & (63))))));
                result_1 = ((RogueInt32)(((((result_1) << (6))) | (((((RogueInt32)(((RogueByte_List__remove_at__Int32( ROGUE_ARG(THIS->input_bytes), 0 )))))) & (63))))));
              }
              else if (ROGUE_COND(((((((RogueInt32)(b1_0))) & (252))) == (248))))
              {
                if ( !(((THIS->input_bytes->count) >= (4))) ) goto _auto_1187;
                result_1 = ((RogueInt32)(((((RogueInt32)(b1_0))) & (3))));
                result_1 = ((RogueInt32)(((((result_1) << (6))) | (((((RogueInt32)(((RogueByte_List__remove_at__Int32( ROGUE_ARG(THIS->input_bytes), 0 )))))) & (63))))));
                result_1 = ((RogueInt32)(((((result_1) << (6))) | (((((RogueInt32)(((RogueByte_List__remove_at__Int32( ROGUE_ARG(THIS->input_bytes), 0 )))))) & (63))))));
                result_1 = ((RogueInt32)(((((result_1) << (6))) | (((((RogueInt32)(((RogueByte_List__remove_at__Int32( ROGUE_ARG(THIS->input_bytes), 0 )))))) & (63))))));
                result_1 = ((RogueInt32)(((((result_1) << (6))) | (((((RogueInt32)(((RogueByte_List__remove_at__Int32( ROGUE_ARG(THIS->input_bytes), 0 )))))) & (63))))));
              }
              else
              {
                if ( !(((THIS->input_bytes->count) >= (5))) ) goto _auto_1187;
                result_1 = ((RogueInt32)(((((RogueInt32)(b1_0))) & (1))));
                result_1 = ((RogueInt32)(((((result_1) << (6))) | (((((RogueInt32)(((RogueByte_List__remove_at__Int32( ROGUE_ARG(THIS->input_bytes), 0 )))))) & (63))))));
                result_1 = ((RogueInt32)(((((result_1) << (6))) | (((((RogueInt32)(((RogueByte_List__remove_at__Int32( ROGUE_ARG(THIS->input_bytes), 0 )))))) & (63))))));
                result_1 = ((RogueInt32)(((((result_1) << (6))) | (((((RogueInt32)(((RogueByte_List__remove_at__Int32( ROGUE_ARG(THIS->input_bytes), 0 )))))) & (63))))));
                result_1 = ((RogueInt32)(((((result_1) << (6))) | (((((RogueInt32)(((RogueByte_List__remove_at__Int32( ROGUE_ARG(THIS->input_bytes), 0 )))))) & (63))))));
                result_1 = ((RogueInt32)(((((result_1) << (6))) | (((((RogueInt32)(((RogueByte_List__remove_at__Int32( ROGUE_ARG(THIS->input_bytes), 0 )))))) & (63))))));
              }
              THIS->next_input_character = RogueOptionalInt32( result_1, true );
            }
            }
          goto _auto_1186;
        }
        _auto_1187:;
        {
          THIS->next_input_character = RogueOptionalInt32( ((RogueInt32)(b1_0)), true );
          }
      }
      _auto_1186:;
      if (ROGUE_COND(((((THIS->input_bytes->count) > (0))) && (((THIS->input_bytes->count) < (6))))))
      {
        RogueConsole__fill_input_queue( ROGUE_ARG(THIS) );
      }
    }
  }
  return (RogueLogical)(THIS->next_input_character.exists);
}

RogueCharacter RogueConsole__read( RogueClassConsole* THIS )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(!(((RogueConsole__has_another( ROGUE_ARG(THIS) ))))))
  {
    return (RogueCharacter)((RogueCharacter)0);
  }
  RogueInt32 result_0 = (THIS->next_input_character.value);
  THIS->next_input_character = (RogueOptionalInt32__create());
  return (RogueCharacter)(((RogueCharacter)(result_0)));
}

RogueClassConsole* RogueConsole__flush( RogueClassConsole* THIS )
{
  ROGUE_GC_CHECK;
  RogueConsole__write__StringBuilder( ROGUE_ARG(THIS), ROGUE_ARG(THIS->output_buffer) );
  RogueStringBuilder__clear( ROGUE_ARG(THIS->output_buffer) );
  return (RogueClassConsole*)(THIS);
}

RogueClassConsole* RogueConsole__print__String( RogueClassConsole* THIS, RogueString* value_0 )
{
  ROGUE_GC_CHECK;
  RogueStringBuilder__print__String( ROGUE_ARG(THIS->output_buffer), value_0 );
  if (ROGUE_COND(((THIS->output_buffer->count) > (1024))))
  {
    RogueConsole__flush( ROGUE_ARG(THIS) );
  }
  return (RogueClassConsole*)(THIS);
}

RogueClassConsole* RogueConsole__println( RogueClassConsole* THIS )
{
  ROGUE_GC_CHECK;
  RogueStringBuilder__print__Character_Logical( ROGUE_ARG(THIS->output_buffer), (RogueCharacter)10, true );
  return (RogueClassConsole*)(((RogueClassConsole*)(RogueConsole__flush( ROGUE_ARG(THIS) ))));
}

RogueClassConsole* RogueConsole__println__String( RogueClassConsole* THIS, RogueString* value_0 )
{
  ROGUE_GC_CHECK;
  return (RogueClassConsole*)(((RogueClassConsole*)(RogueConsole__println( ROGUE_ARG(((RogueClassConsole*)(RogueConsole__print__String( ROGUE_ARG(THIS), value_0 )))) ))));
}

RogueClassConsole* RogueConsole__write__StringBuilder( RogueClassConsole* THIS, RogueStringBuilder* buffer_0 )
{
  ROGUE_GC_CHECK;
  fwrite( buffer_0->utf8->data->as_bytes, 1, buffer_0->utf8->count, stdout );
  fflush( stdout );

  return (RogueClassConsole*)(THIS);
}

void RogueConsole__fill_input_queue( RogueClassConsole* THIS )
{
  ROGUE_GC_CHECK;
  RogueInt32 n_0 = 0;
  char bytes[8];
  n_0 = (RogueInt32) ROGUE_READ_CALL( STDIN_FILENO, &bytes, 8 );

  if (ROGUE_COND(((n_0) > (0))))
  {
    {
      RogueInt32 i_1 = (0);
      RogueInt32 _auto_201_2 = (n_0);
      for (;ROGUE_COND(((i_1) < (_auto_201_2)));++i_1)
      {
        ROGUE_GC_CHECK;
        RogueByte_List__add__Byte( ROGUE_ARG(THIS->input_bytes), ROGUE_ARG(((RogueByte)(((RogueByte)bytes[i_1])))) );
      }
    }
  }
}

void RogueConsole__reset_input_mode( RogueClassConsole* THIS )
{
  ROGUE_GC_CHECK;
  tcsetattr( STDIN_FILENO, TCSANOW, &THIS->original_terminal_settings );
  fcntl( STDIN_FILENO, F_SETFL, THIS->original_stdin_flags );

}

RogueInt32 RogueConsole__width( RogueClassConsole* THIS )
{
  ROGUE_GC_CHECK;
  struct winsize sz;
  ioctl( STDOUT_FILENO, TIOCGWINSZ, &sz );
  return sz.ws_col;

}

RogueClassPrintWriter_output_buffer_* RoguePrintWriter_output_buffer___close( RogueObject* THIS )
{
  switch (THIS->type->index)
  {
    case 45:
      return RogueConsole__close( (RogueClassConsole*)THIS );
    case 47:
      return (RogueClassPrintWriter_output_buffer_*)RogueConsoleErrorPrinter__close( (RogueClassConsoleErrorPrinter*)THIS );
    default:
      return 0;
  }
}

RogueClassPrintWriter_output_buffer_* RoguePrintWriter_output_buffer___flush( RogueObject* THIS )
{
  switch (THIS->type->index)
  {
    case 45:
      return (RogueClassPrintWriter_output_buffer_*)RogueConsole__flush( (RogueClassConsole*)THIS );
    case 47:
      return (RogueClassPrintWriter_output_buffer_*)RogueConsoleErrorPrinter__flush( (RogueClassConsoleErrorPrinter*)THIS );
    default:
      return 0;
  }
}

RogueClassPrintWriter_output_buffer_* RoguePrintWriter_output_buffer___println__String( RogueObject* THIS, RogueString* value_0 )
{
  switch (THIS->type->index)
  {
    case 45:
      return (RogueClassPrintWriter_output_buffer_*)RogueConsole__println__String( (RogueClassConsole*)THIS, value_0 );
    case 47:
      return (RogueClassPrintWriter_output_buffer_*)RogueConsoleErrorPrinter__println__String( (RogueClassConsoleErrorPrinter*)THIS, value_0 );
    default:
      return 0;
  }
}

RogueClassPrintWriter_output_buffer_* RoguePrintWriter_output_buffer___write__StringBuilder( RogueObject* THIS, RogueStringBuilder* buffer_0 )
{
  switch (THIS->type->index)
  {
    case 45:
      return (RogueClassPrintWriter_output_buffer_*)RogueConsole__write__StringBuilder( (RogueClassConsole*)THIS, buffer_0 );
    case 47:
      return (RogueClassPrintWriter_output_buffer_*)RogueConsoleErrorPrinter__write__StringBuilder( (RogueClassConsoleErrorPrinter*)THIS, buffer_0 );
    default:
      return 0;
  }
}

RogueClassConsoleErrorPrinter* RogueConsoleErrorPrinter__init_object( RogueClassConsoleErrorPrinter* THIS )
{
  ROGUE_GC_CHECK;
  RogueObject__init_object( ROGUE_ARG(((RogueObject*)THIS)) );
  THIS->output_buffer = ((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )));
  return (RogueClassConsoleErrorPrinter*)(THIS);
}

RogueString* RogueConsoleErrorPrinter__type_name( RogueClassConsoleErrorPrinter* THIS )
{
  return (RogueString*)(Rogue_literal_strings[153]);
}

RogueClassConsoleErrorPrinter* RogueConsoleErrorPrinter__close( RogueClassConsoleErrorPrinter* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueClassConsoleErrorPrinter*)(THIS);
}

RogueClassConsoleErrorPrinter* RogueConsoleErrorPrinter__flush( RogueClassConsoleErrorPrinter* THIS )
{
  ROGUE_GC_CHECK;
  RogueConsoleErrorPrinter__write__StringBuilder( ROGUE_ARG(THIS), ROGUE_ARG(THIS->output_buffer) );
  RogueStringBuilder__clear( ROGUE_ARG(THIS->output_buffer) );
  return (RogueClassConsoleErrorPrinter*)(THIS);
}

RogueClassConsoleErrorPrinter* RogueConsoleErrorPrinter__print__String( RogueClassConsoleErrorPrinter* THIS, RogueString* value_0 )
{
  ROGUE_GC_CHECK;
  RogueStringBuilder__print__String( ROGUE_ARG(THIS->output_buffer), value_0 );
  if (ROGUE_COND(((THIS->output_buffer->count) > (1024))))
  {
    RogueConsoleErrorPrinter__flush( ROGUE_ARG(THIS) );
  }
  return (RogueClassConsoleErrorPrinter*)(THIS);
}

RogueClassConsoleErrorPrinter* RogueConsoleErrorPrinter__println( RogueClassConsoleErrorPrinter* THIS )
{
  ROGUE_GC_CHECK;
  RogueStringBuilder__print__Character_Logical( ROGUE_ARG(THIS->output_buffer), (RogueCharacter)10, true );
  return (RogueClassConsoleErrorPrinter*)(((RogueClassConsoleErrorPrinter*)(RogueConsoleErrorPrinter__flush( ROGUE_ARG(THIS) ))));
}

RogueClassConsoleErrorPrinter* RogueConsoleErrorPrinter__println__String( RogueClassConsoleErrorPrinter* THIS, RogueString* value_0 )
{
  ROGUE_GC_CHECK;
  return (RogueClassConsoleErrorPrinter*)(((RogueClassConsoleErrorPrinter*)(RogueConsoleErrorPrinter__println( ROGUE_ARG(((RogueClassConsoleErrorPrinter*)(RogueConsoleErrorPrinter__print__String( ROGUE_ARG(THIS), value_0 )))) ))));
}

RogueClassConsoleErrorPrinter* RogueConsoleErrorPrinter__write__StringBuilder( RogueClassConsoleErrorPrinter* THIS, RogueStringBuilder* buffer_0 )
{
  ROGUE_GC_CHECK;
  fwrite( buffer_0->utf8->data->as_bytes, 1, buffer_0->utf8->count, stderr );
  fflush( stderr );

  return (RogueClassConsoleErrorPrinter*)(THIS);
}

RogueClassPrimitiveWorkBuffer* RoguePrimitiveWorkBuffer__init_object( RogueClassPrimitiveWorkBuffer* THIS )
{
  ROGUE_GC_CHECK;
  RogueStringBuilder__init_object( ROGUE_ARG(((RogueStringBuilder*)THIS)) );
  return (RogueClassPrimitiveWorkBuffer*)(THIS);
}

RogueString* RoguePrimitiveWorkBuffer__type_name( RogueClassPrimitiveWorkBuffer* THIS )
{
  return (RogueString*)(Rogue_literal_strings[192]);
}

RogueClassMath* RogueMath__init_object( RogueClassMath* THIS )
{
  ROGUE_GC_CHECK;
  RogueObject__init_object( ROGUE_ARG(((RogueObject*)THIS)) );
  return (RogueClassMath*)(THIS);
}

RogueString* RogueMath__type_name( RogueClassMath* THIS )
{
  return (RogueString*)(Rogue_literal_strings[143]);
}

RogueClassFunction_221* RogueFunction_221__init_object( RogueClassFunction_221* THIS )
{
  ROGUE_GC_CHECK;
  Rogue_Function_____init_object( ROGUE_ARG(((RogueClass_Function___*)THIS)) );
  return (RogueClassFunction_221*)(THIS);
}

RogueString* RogueFunction_221__type_name( RogueClassFunction_221* THIS )
{
  return (RogueString*)(Rogue_literal_strings[200]);
}

void RogueFunction_221__call( RogueClassFunction_221* THIS )
{
  RogueGlobal__flush( ((RogueClassGlobal*)ROGUE_SINGLETON(Global)) );
}

RogueClassSystem* RogueSystem__init_object( RogueClassSystem* THIS )
{
  ROGUE_GC_CHECK;
  RogueObject__init_object( ROGUE_ARG(((RogueObject*)THIS)) );
  return (RogueClassSystem*)(THIS);
}

RogueString* RogueSystem__type_name( RogueClassSystem* THIS )
{
  return (RogueString*)(Rogue_literal_strings[145]);
}

RogueClassError* RogueError__init_object( RogueClassError* THIS )
{
  ROGUE_GC_CHECK;
  RogueException__init_object( ROGUE_ARG(((RogueException*)THIS)) );
  return (RogueClassError*)(THIS);
}

RogueString* RogueError__type_name( RogueClassError* THIS )
{
  return (RogueString*)(Rogue_literal_strings[202]);
}

RogueClassError* RogueError___throw( RogueClassError* THIS )
{
  ROGUE_GC_CHECK;
  throw THIS;

}

RogueClassFile* RogueFile__init_object( RogueClassFile* THIS )
{
  ROGUE_GC_CHECK;
  RogueObject__init_object( ROGUE_ARG(((RogueObject*)THIS)) );
  return (RogueClassFile*)(THIS);
}

RogueString* RogueFile__to_String( RogueClassFile* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(THIS->filepath);
}

RogueString* RogueFile__type_name( RogueClassFile* THIS )
{
  return (RogueString*)(Rogue_literal_strings[146]);
}

RogueClassFile* RogueFile__init__String( RogueClassFile* THIS, RogueString* _auto_231_0 )
{
  ROGUE_GC_CHECK;
  THIS->filepath = _auto_231_0;
  return (RogueClassFile*)(THIS);
}

RogueString* RogueFile__filename( RogueClassFile* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueString*)((RogueFile__filename__String( ROGUE_ARG(THIS->filepath) )));
}

RogueClassLineReader* RogueLineReader__init_object( RogueClassLineReader* THIS )
{
  ROGUE_GC_CHECK;
  RogueObject__init_object( ROGUE_ARG(((RogueObject*)THIS)) );
  THIS->buffer = ((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )));
  return (RogueClassLineReader*)(THIS);
}

RogueString* RogueLineReader__type_name( RogueClassLineReader* THIS )
{
  return (RogueString*)(Rogue_literal_strings[147]);
}

RogueClassLineReader* RogueLineReader__close( RogueClassLineReader* THIS )
{
  ROGUE_GC_CHECK;
  RogueReader_Character___close( ROGUE_ARG(((((RogueObject*)THIS->source)))) );
  return (RogueClassLineReader*)(THIS);
}

RogueLogical RogueLineReader__has_another( RogueClassLineReader* THIS )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((void*)THIS->next) == ((void*)NULL)))
  {
    THIS->next = ((RogueString*)(RogueLineReader__prepare_next( ROGUE_ARG(THIS) )));
  }
  return (RogueLogical)(!!(THIS->next));
}

RogueString* RogueLineReader__read( RogueClassLineReader* THIS )
{
  ROGUE_GC_CHECK;
  ROGUE_DEF_LOCAL_REF(RogueString*,result_0,(THIS->next));
  THIS->next = ((RogueString*)(RogueLineReader__prepare_next( ROGUE_ARG(THIS) )));
  THIS->position = ((THIS->position) + (1));
  return (RogueString*)(result_0);
}

RogueClassLineReader* RogueLineReader__init__Reader_Character_( RogueClassLineReader* THIS, RogueClassReader_Character_* _auto_234_0 )
{
  ROGUE_GC_CHECK;
  THIS->source = _auto_234_0;
  THIS->next = ((RogueString*)(RogueLineReader__prepare_next( ROGUE_ARG(THIS) )));
  return (RogueClassLineReader*)(THIS);
}

RogueClassLineReader* RogueLineReader__init__Reader_Byte_( RogueClassLineReader* THIS, RogueClassReader_Byte_* reader_0 )
{
  ROGUE_GC_CHECK;
  RogueLineReader__init__Reader_Character_( ROGUE_ARG(THIS), ROGUE_ARG(((((RogueClassReader_Character_*)(((RogueClassUTF8Reader*)(RogueUTF8Reader__init__Reader_Byte_( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassUTF8Reader*,ROGUE_CREATE_OBJECT(UTF8Reader))), ((reader_0)) )))))))) );
  return (RogueClassLineReader*)(THIS);
}

RogueClassLineReader* RogueLineReader__init__File( RogueClassLineReader* THIS, RogueClassFile* file_0 )
{
  ROGUE_GC_CHECK;
  RogueLineReader__init__Reader_Byte_( ROGUE_ARG(THIS), ROGUE_ARG(((((RogueClassReader_Byte_*)((RogueFile__reader__String( ROGUE_ARG(file_0->filepath) ))))))) );
  return (RogueClassLineReader*)(THIS);
}

RogueString* RogueLineReader__prepare_next( RogueClassLineReader* THIS )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(!((RogueReader_Character___has_another( ROGUE_ARG(((((RogueObject*)THIS->source)))) )))))
  {
    return (RogueString*)(((RogueString*)(NULL)));
  }
  THIS->prev = (RogueCharacter)0;
  RogueStringBuilder__clear( ROGUE_ARG(THIS->buffer) );
  while (ROGUE_COND((RogueReader_Character___has_another( ROGUE_ARG(((((RogueObject*)THIS->source)))) ))))
  {
    ROGUE_GC_CHECK;
    RogueCharacter ch_0 = ((RogueReader_Character___read( ROGUE_ARG(((((RogueObject*)THIS->source)))) )));
    if (ROGUE_COND(((ch_0) == ((RogueCharacter)10))))
    {
      THIS->prev = (RogueCharacter)10;
      return (RogueString*)(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(THIS->buffer) ))));
    }
    RogueStringBuilder__print__Character_Logical( ROGUE_ARG(THIS->buffer), ch_0, true );
  }
  return (RogueString*)(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(THIS->buffer) ))));
}

RogueClassReader_Byte_* RogueReader_Byte___close( RogueObject* THIS )
{
  switch (THIS->type->index)
  {
    case 76:
      return (RogueClassReader_Byte_*)RogueFileReader__close( (RogueClassFileReader*)THIS );
    default:
      return 0;
  }
}

RogueLogical RogueReader_Byte___has_another( RogueObject* THIS )
{
  switch (THIS->type->index)
  {
    case 76:
      return RogueFileReader__has_another( (RogueClassFileReader*)THIS );
    default:
      return 0;
  }
}

RogueByte RogueReader_Byte___read( RogueObject* THIS )
{
  switch (THIS->type->index)
  {
    case 76:
      return RogueFileReader__read( (RogueClassFileReader*)THIS );
    default:
      return 0;
  }
}

RogueClassFileWriter* RogueFileWriter__init_object( RogueClassFileWriter* THIS )
{
  ROGUE_GC_CHECK;
  RogueObject__init_object( ROGUE_ARG(((RogueObject*)THIS)) );
  THIS->buffer = ((RogueByte_List*)(RogueByte_List__init__Int32( ROGUE_ARG(ROGUE_CREATE_REF(RogueByte_List*,ROGUE_CREATE_OBJECT(Byte_List))), 1024 )));
  return (RogueClassFileWriter*)(THIS);
}

RogueString* RogueFileWriter__type_name( RogueClassFileWriter* THIS )
{
  return (RogueString*)(Rogue_literal_strings[148]);
}

RogueClassFileWriter* RogueFileWriter__close( RogueClassFileWriter* THIS )
{
  ROGUE_GC_CHECK;
  RogueFileWriter__flush( ROGUE_ARG(THIS) );
  if (ROGUE_COND(!!(THIS->fp)))
  {
    fclose( THIS->fp ); THIS->fp = 0;

    RogueSystem__sync_storage();
  }
  return (RogueClassFileWriter*)(THIS);
}

RogueClassFileWriter* RogueFileWriter__flush( RogueClassFileWriter* THIS )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((((THIS->buffer->count) == (0))) || (!(!!(THIS->fp))))))
  {
    return (RogueClassFileWriter*)(THIS);
  }
  fwrite( THIS->buffer->data->as_bytes, 1, THIS->buffer->count, THIS->fp );
  fflush( THIS->fp );

  RogueByte_List__clear( ROGUE_ARG(THIS->buffer) );
  return (RogueClassFileWriter*)(THIS);
}

RogueClassFileWriter* RogueFileWriter__write__Byte( RogueClassFileWriter* THIS, RogueByte ch_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(!(!!(THIS->fp))))
  {
    return (RogueClassFileWriter*)(THIS);
  }
  THIS->position = ((THIS->position) + (1));
  RogueByte_List__add__Byte( ROGUE_ARG(THIS->buffer), ch_0 );
  if (ROGUE_COND(((THIS->buffer->count) == (1024))))
  {
    return (RogueClassFileWriter*)(((RogueClassFileWriter*)(RogueFileWriter__flush( ROGUE_ARG(THIS) ))));
  }
  return (RogueClassFileWriter*)(THIS);
}

RogueClassFileWriter* RogueFileWriter__write__Byte_List( RogueClassFileWriter* THIS, RogueByte_List* bytes_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(!(!!(THIS->fp))))
  {
    return (RogueClassFileWriter*)(THIS);
  }
  if (ROGUE_COND(((bytes_0->count) < (1024))))
  {
    {
      ROGUE_DEF_LOCAL_REF(RogueByte_List*,_auto_1239_0,(bytes_0));
      RogueInt32 _auto_1240_0 = (0);
      RogueInt32 _auto_1241_0 = (((_auto_1239_0->count) - (1)));
      for (;ROGUE_COND(((_auto_1240_0) <= (_auto_1241_0)));++_auto_1240_0)
      {
        ROGUE_GC_CHECK;
        RogueByte byte_0 = (_auto_1239_0->data->as_bytes[_auto_1240_0]);
        RogueFileWriter__write__Byte( ROGUE_ARG(THIS), byte_0 );
      }
    }
  }
  else
  {
    RogueFileWriter__flush( ROGUE_ARG(THIS) );
    THIS->position += bytes_0->count;
    fwrite( bytes_0->data->as_bytes, 1, bytes_0->count, THIS->fp );

  }
  return (RogueClassFileWriter*)(THIS);
}

RogueClassFileWriter* RogueFileWriter__init__String_Logical( RogueClassFileWriter* THIS, RogueString* _filepath_0, RogueLogical append_1 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(!(((RogueFileWriter__open__String_Logical( ROGUE_ARG(THIS), _filepath_0, append_1 ))))))
  {
    throw ((RogueClassIOError*)(RogueIOError___throw( ROGUE_ARG(((RogueClassIOError*)(((RogueException*)Rogue_call_ROGUEM4( 13, ROGUE_ARG(((RogueException*)ROGUE_CREATE_REF(RogueClassIOError*,ROGUE_CREATE_OBJECT(IOError)))), ROGUE_ARG(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[24] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], _filepath_0 ))) )))), Rogue_literal_strings[48] )))) )))) ))))) )));
  }
  return (RogueClassFileWriter*)(THIS);
}

void RogueFileWriter__on_cleanup( RogueClassFileWriter* THIS )
{
  ROGUE_GC_CHECK;
  RogueFileWriter__close( ROGUE_ARG(THIS) );
}

RogueLogical RogueFileWriter__open__String_Logical( RogueClassFileWriter* THIS, RogueString* _auto_243_0, RogueLogical append_1 )
{
  ROGUE_GC_CHECK;
  THIS->filepath = _auto_243_0;
  RogueFileWriter__close( ROGUE_ARG(THIS) );
  THIS->error = false;
  if (ROGUE_COND(append_1))
  {
    THIS->fp = fopen( (char*)THIS->filepath->utf8, "ab" );

  }
  else
  {
    THIS->fp = fopen( (char*)THIS->filepath->utf8, "wb" );

  }
  THIS->error = !(THIS->fp);

  return (RogueLogical)(!(THIS->error));
}

RogueClassFileWriter* RogueFileWriter__write__String( RogueClassFileWriter* THIS, RogueString* data_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(!(!!(THIS->fp))))
  {
    return (RogueClassFileWriter*)(THIS);
  }
  if (ROGUE_COND(((data_0->character_count) < (1024))))
  {
    {
      RogueInt32 i_1 = (0);
      RogueInt32 _auto_241_2 = (data_0->byte_count);
      for (;ROGUE_COND(((i_1) < (_auto_241_2)));++i_1)
      {
        ROGUE_GC_CHECK;
        RogueFileWriter__write__Byte( ROGUE_ARG(THIS), ROGUE_ARG(data_0->utf8[ i_1 ]) );
      }
    }
  }
  else
  {
    RogueFileWriter__flush( ROGUE_ARG(THIS) );
    THIS->position += data_0->byte_count;
    fwrite( data_0->utf8, 1, data_0->byte_count, THIS->fp );

  }
  return (RogueClassFileWriter*)(THIS);
}

RogueClassWriter_Byte_* RogueWriter_Byte___close( RogueObject* THIS )
{
  switch (THIS->type->index)
  {
    case 56:
      return (RogueClassWriter_Byte_*)RogueFileWriter__close( (RogueClassFileWriter*)THIS );
    default:
      return 0;
  }
}

RogueClassWriter_Byte_* RogueWriter_Byte___flush( RogueObject* THIS )
{
  switch (THIS->type->index)
  {
    case 56:
      return (RogueClassWriter_Byte_*)RogueFileWriter__flush( (RogueClassFileWriter*)THIS );
    default:
      return 0;
  }
}

RogueClassWriter_Byte_* RogueWriter_Byte___write__Byte_List( RogueObject* THIS, RogueByte_List* list_0 )
{
  switch (THIS->type->index)
  {
    case 56:
      return (RogueClassWriter_Byte_*)RogueFileWriter__write__Byte_List( (RogueClassFileWriter*)THIS, list_0 );
    default:
      return 0;
  }
}

RogueClassScanner* RogueScanner__init_object( RogueClassScanner* THIS )
{
  ROGUE_GC_CHECK;
  RogueObject__init_object( ROGUE_ARG(((RogueObject*)THIS)) );
  return (RogueClassScanner*)(THIS);
}

RogueString* RogueScanner__type_name( RogueClassScanner* THIS )
{
  return (RogueString*)(Rogue_literal_strings[149]);
}

RogueClassScanner* RogueScanner__close( RogueClassScanner* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueClassScanner*)(THIS);
}

RogueLogical RogueScanner__has_another( RogueClassScanner* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueLogical)(((THIS->position) < (THIS->count)));
}

RogueCharacter RogueScanner__peek( RogueClassScanner* THIS )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((THIS->position) == (THIS->count))))
  {
    return (RogueCharacter)((RogueCharacter)0);
  }
  return (RogueCharacter)(THIS->data->data->as_characters[THIS->position]);
}

RogueCharacter RogueScanner__read( RogueClassScanner* THIS )
{
  ROGUE_GC_CHECK;
  RogueCharacter result_0 = (THIS->data->data->as_characters[THIS->position]);
  THIS->position = ((THIS->position) + (1));
  if (ROGUE_COND(((((RogueInt32)(result_0))) == (10))))
  {
    ++THIS->line;
    THIS->column = 1;
  }
  else
  {
    ++THIS->column;
  }
  return (RogueCharacter)(result_0);
}

RogueClassScanner* RogueScanner__init__String_Int32_Logical( RogueClassScanner* THIS, RogueString* source_0, RogueInt32 _auto_251_1, RogueLogical preserve_crlf_2 )
{
  ROGUE_GC_CHECK;
  THIS->spaces_per_tab = _auto_251_1;
  RogueInt32 tab_count_3 = (0);
  if (ROGUE_COND(!!(THIS->spaces_per_tab)))
  {
    {
      ROGUE_DEF_LOCAL_REF(RogueString*,_auto_1242_0,(source_0));
      RogueInt32 _auto_1243_0 = (0);
      RogueInt32 _auto_1244_0 = (((_auto_1242_0->character_count) - (1)));
      for (;ROGUE_COND(((_auto_1243_0) <= (_auto_1244_0)));++_auto_1243_0)
      {
        ROGUE_GC_CHECK;
        RogueCharacter b_0 = (RogueString_character_at(_auto_1242_0,_auto_1243_0));
        if (ROGUE_COND(((b_0) == ((RogueCharacter)9))))
        {
          ++tab_count_3;
        }
      }
    }
  }
  THIS->data = ((RogueCharacter_List*)(RogueCharacter_List__init__Int32( ROGUE_ARG(ROGUE_CREATE_REF(RogueCharacter_List*,ROGUE_CREATE_OBJECT(Character_List))), ROGUE_ARG(((source_0->character_count) + (tab_count_3))) )));
  {
    ROGUE_DEF_LOCAL_REF(RogueString*,_auto_1245_0,(source_0));
    RogueInt32 _auto_1246_0 = (0);
    RogueInt32 _auto_1247_0 = (((_auto_1245_0->character_count) - (1)));
    for (;ROGUE_COND(((_auto_1246_0) <= (_auto_1247_0)));++_auto_1246_0)
    {
      ROGUE_GC_CHECK;
      RogueCharacter b_0 = (RogueString_character_at(_auto_1245_0,_auto_1246_0));
      if (ROGUE_COND(((((b_0) == ((RogueCharacter)9))) && (!!(THIS->spaces_per_tab)))))
      {
        {
          RogueInt32 _auto_245_4 = (1);
          RogueInt32 _auto_246_5 = (THIS->spaces_per_tab);
          for (;ROGUE_COND(((_auto_245_4) <= (_auto_246_5)));++_auto_245_4)
          {
            ROGUE_GC_CHECK;
            RogueCharacter_List__add__Character( ROGUE_ARG(THIS->data), (RogueCharacter)' ' );
          }
        }
      }
      else
      {
        RogueCharacter_List__add__Character( ROGUE_ARG(THIS->data), b_0 );
      }
    }
  }
  if (ROGUE_COND(!(preserve_crlf_2)))
  {
    RogueScanner__convert_crlf_to_newline( ROGUE_ARG(THIS) );
  }
  THIS->count = THIS->data->count;
  THIS->line = 1;
  THIS->column = 1;
  THIS->position = 0;
  return (RogueClassScanner*)(THIS);
}

RogueLogical RogueScanner__consume__Character( RogueClassScanner* THIS, RogueCharacter ch_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((((RogueScanner__peek( ROGUE_ARG(THIS) )))) != (ch_0))))
  {
    return (RogueLogical)(false);
  }
  RogueScanner__read( ROGUE_ARG(THIS) );
  return (RogueLogical)(true);
}

RogueLogical RogueScanner__consume_eols( RogueClassScanner* THIS )
{
  ROGUE_GC_CHECK;
  RogueLogical found_0 = (false);
  while (ROGUE_COND(((RogueScanner__consume__Character( ROGUE_ARG(THIS), (RogueCharacter)10 )))))
  {
    ROGUE_GC_CHECK;
    found_0 = ((RogueLogical)(true));
  }
  return (RogueLogical)(found_0);
}

RogueLogical RogueScanner__consume_spaces( RogueClassScanner* THIS )
{
  ROGUE_GC_CHECK;
  RogueLogical found_0 = (false);
  while (ROGUE_COND(((RogueScanner__consume__Character( ROGUE_ARG(THIS), (RogueCharacter)' ' )))))
  {
    ROGUE_GC_CHECK;
    found_0 = ((RogueLogical)(true));
  }
  return (RogueLogical)(found_0);
}

RogueClassScanner* RogueScanner__convert_crlf_to_newline( RogueClassScanner* THIS )
{
  ROGUE_GC_CHECK;
  {
    ROGUE_DEF_LOCAL_REF(RogueClassListRewriter_Character_*,writer_0,(((RogueClassListRewriter_Character_*)(RogueCharacter_List__rewriter( ROGUE_ARG(THIS->data) )))));
    while (ROGUE_COND(((RogueListRewriter_Character___has_another( writer_0 )))))
    {
      ROGUE_GC_CHECK;
      RogueCharacter ch_0 = (((RogueListRewriter_Character___read( writer_0 ))));
      if (ROGUE_COND(((((RogueInt32)(ch_0))) != (13))))
      {
        RogueListRewriter_Character___write__Character( writer_0, ch_0 );
      }
    }
  }
  return (RogueClassScanner*)(THIS);
}

RogueClassJSONParser* RogueJSONParser__init_object( RogueClassJSONParser* THIS )
{
  ROGUE_GC_CHECK;
  RogueObject__init_object( ROGUE_ARG(((RogueObject*)THIS)) );
  return (RogueClassJSONParser*)(THIS);
}

RogueString* RogueJSONParser__type_name( RogueClassJSONParser* THIS )
{
  return (RogueString*)(Rogue_literal_strings[150]);
}

RogueClassJSONParser* RogueJSONParser__init__String( RogueClassJSONParser* THIS, RogueString* json_0 )
{
  ROGUE_GC_CHECK;
  THIS->reader = ((RogueClassScanner*)(RogueScanner__init__String_Int32_Logical( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassScanner*,ROGUE_CREATE_OBJECT(Scanner))), json_0, 0, false )));
  return (RogueClassJSONParser*)(THIS);
}

RogueClassJSONParser* RogueJSONParser__init__Scanner( RogueClassJSONParser* THIS, RogueClassScanner* _auto_257_0 )
{
  ROGUE_GC_CHECK;
  THIS->reader = _auto_257_0;
  return (RogueClassJSONParser*)(THIS);
}

RogueLogical RogueJSONParser__consume__Character( RogueClassJSONParser* THIS, RogueCharacter ch_0 )
{
  ROGUE_GC_CHECK;
  return (RogueLogical)(((RogueScanner__consume__Character( ROGUE_ARG(THIS->reader), ch_0 ))));
}

RogueLogical RogueJSONParser__has_another( RogueClassJSONParser* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueLogical)(((RogueScanner__has_another( ROGUE_ARG(THIS->reader) ))));
}

RogueLogical RogueJSONParser__next_is__Character( RogueClassJSONParser* THIS, RogueCharacter ch_0 )
{
  ROGUE_GC_CHECK;
  return (RogueLogical)(((((RogueScanner__peek( ROGUE_ARG(THIS->reader) )))) == (ch_0)));
}

RogueClassValue* RogueJSONParser__parse_value( RogueClassJSONParser* THIS )
{
  ROGUE_GC_CHECK;
  RogueJSONParser__consume_spaces_and_eols( ROGUE_ARG(THIS) );
  if (ROGUE_COND(!(((RogueScanner__has_another( ROGUE_ARG(THIS->reader) ))))))
  {
    return (RogueClassValue*)(((RogueClassValue*)(((RogueClassUndefinedValue*)ROGUE_SINGLETON(UndefinedValue)))));
  }
  RogueCharacter ch_0 = (((RogueScanner__peek( ROGUE_ARG(THIS->reader) ))));
  if (ROGUE_COND(((ch_0) == ((RogueCharacter)'{'))))
  {
    return (RogueClassValue*)(((RogueClassValue*)(RogueJSONParser__parse_table__Character_Character( ROGUE_ARG(THIS), (RogueCharacter)'{', (RogueCharacter)'}' ))));
  }
  if (ROGUE_COND(((ch_0) == ((RogueCharacter)'['))))
  {
    return (RogueClassValue*)(((RogueClassValue*)(RogueJSONParser__parse_list__Character_Character( ROGUE_ARG(THIS), (RogueCharacter)'[', (RogueCharacter)']' ))));
  }
  if (ROGUE_COND(((ch_0) == ((RogueCharacter)'-'))))
  {
    return (RogueClassValue*)(((RogueClassValue*)(RogueJSONParser__parse_number( ROGUE_ARG(THIS) ))));
  }
  if (ROGUE_COND(((((ch_0) >= ((RogueCharacter)'0'))) && (((ch_0) <= ((RogueCharacter)'9'))))))
  {
    return (RogueClassValue*)(((RogueClassValue*)(RogueJSONParser__parse_number( ROGUE_ARG(THIS) ))));
  }
  if (ROGUE_COND(((((ch_0) == ((RogueCharacter)'"'))) || (((ch_0) == ((RogueCharacter)'\''))))))
  {
    ROGUE_DEF_LOCAL_REF(RogueString*,result_1,(((RogueString*)(RogueJSONParser__parse_string( ROGUE_ARG(THIS) )))));
    if (ROGUE_COND(((result_1->character_count) == (0))))
    {
      return (RogueClassValue*)(((RogueClassValue*)(RogueStringValue_empty_string)));
    }
    RogueCharacter first_ch_2 = (RogueString_character_at(result_1,0));
    if (ROGUE_COND(((((first_ch_2) == ((RogueCharacter)'t'))) && ((RogueString__operatorEQUALSEQUALS__String_String( result_1, Rogue_literal_strings[22] ))))))
    {
      return (RogueClassValue*)(((RogueClassValue*)(RogueLogicalValue_true_value)));
    }
    if (ROGUE_COND(((((first_ch_2) == ((RogueCharacter)'f'))) && ((RogueString__operatorEQUALSEQUALS__String_String( result_1, Rogue_literal_strings[23] ))))))
    {
      return (RogueClassValue*)(((RogueClassValue*)(RogueLogicalValue_false_value)));
    }
    if (ROGUE_COND(((((first_ch_2) == ((RogueCharacter)'n'))) && ((RogueString__operatorEQUALSEQUALS__String_String( result_1, Rogue_literal_strings[1] ))))))
    {
      return (RogueClassValue*)(((RogueClassValue*)(((RogueClassNullValue*)ROGUE_SINGLETON(NullValue)))));
    }
    return (RogueClassValue*)(((RogueClassValue*)(((RogueClassStringValue*)(RogueStringValue__init__String( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassStringValue*,ROGUE_CREATE_OBJECT(StringValue))), result_1 ))))));
  }
  else if (ROGUE_COND(((RogueJSONParser__next_is_identifier( ROGUE_ARG(THIS) )))))
  {
    ROGUE_DEF_LOCAL_REF(RogueString*,result_3,(((RogueString*)(RogueJSONParser__parse_identifier( ROGUE_ARG(THIS) )))));
    if (ROGUE_COND(((result_3->character_count) == (0))))
    {
      return (RogueClassValue*)(((RogueClassValue*)(RogueStringValue_empty_string)));
    }
    RogueCharacter first_ch_4 = (RogueString_character_at(result_3,0));
    if (ROGUE_COND(((((first_ch_4) == ((RogueCharacter)'t'))) && ((RogueString__operatorEQUALSEQUALS__String_String( result_3, Rogue_literal_strings[22] ))))))
    {
      return (RogueClassValue*)(((RogueClassValue*)(RogueLogicalValue_true_value)));
    }
    if (ROGUE_COND(((((first_ch_4) == ((RogueCharacter)'f'))) && ((RogueString__operatorEQUALSEQUALS__String_String( result_3, Rogue_literal_strings[23] ))))))
    {
      return (RogueClassValue*)(((RogueClassValue*)(RogueLogicalValue_false_value)));
    }
    if (ROGUE_COND(((((first_ch_4) == ((RogueCharacter)'n'))) && ((RogueString__operatorEQUALSEQUALS__String_String( result_3, Rogue_literal_strings[1] ))))))
    {
      return (RogueClassValue*)(((RogueClassValue*)(((RogueClassNullValue*)ROGUE_SINGLETON(NullValue)))));
    }
    return (RogueClassValue*)(((RogueClassValue*)(((RogueClassStringValue*)(RogueStringValue__init__String( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassStringValue*,ROGUE_CREATE_OBJECT(StringValue))), result_3 ))))));
  }
  else
  {
    return (RogueClassValue*)(((RogueClassValue*)(((RogueClassUndefinedValue*)ROGUE_SINGLETON(UndefinedValue)))));
  }
}

RogueClassValue* RogueJSONParser__parse_table__Character_Character( RogueClassJSONParser* THIS, RogueCharacter open_ch_0, RogueCharacter close_ch_1 )
{
  ROGUE_GC_CHECK;
  RogueJSONParser__consume_spaces_and_eols( ROGUE_ARG(THIS) );
  if (ROGUE_COND(!(((RogueScanner__consume__Character( ROGUE_ARG(THIS->reader), open_ch_0 ))))))
  {
    return (RogueClassValue*)(((RogueClassValue*)(((RogueClassUndefinedValue*)ROGUE_SINGLETON(UndefinedValue)))));
  }
  RogueJSONParser__consume_spaces_and_eols( ROGUE_ARG(THIS) );
  ROGUE_DEF_LOCAL_REF(RogueClassValueTable*,table_2,(((RogueClassValueTable*)(RogueValueTable__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassValueTable*,ROGUE_CREATE_OBJECT(ValueTable))) )))));
  if (ROGUE_COND(((RogueScanner__consume__Character( ROGUE_ARG(THIS->reader), close_ch_1 )))))
  {
    return (RogueClassValue*)(((RogueClassValue*)(table_2)));
  }
  RogueInt32 prev_pos_3 = (THIS->reader->position);
  RogueLogical first_4 = (true);
  while (ROGUE_COND(((((first_4) || (((RogueScanner__consume__Character( ROGUE_ARG(THIS->reader), (RogueCharacter)',' )))))) || (((((((RogueScanner__has_another( ROGUE_ARG(THIS->reader) )))) && (((((RogueScanner__peek( ROGUE_ARG(THIS->reader) )))) != (close_ch_1))))) && (((THIS->reader->position) > (prev_pos_3))))))))
  {
    ROGUE_GC_CHECK;
    first_4 = ((RogueLogical)(false));
    prev_pos_3 = ((RogueInt32)(THIS->reader->position));
    RogueJSONParser__consume_spaces_and_eols( ROGUE_ARG(THIS) );
    if (ROGUE_COND(((RogueJSONParser__next_is_identifier( ROGUE_ARG(THIS) )))))
    {
      ROGUE_DEF_LOCAL_REF(RogueString*,key_5,(((RogueString*)(RogueJSONParser__parse_identifier( ROGUE_ARG(THIS) )))));
      RogueJSONParser__consume_spaces_and_eols( ROGUE_ARG(THIS) );
      if (ROGUE_COND(!!(key_5->character_count)))
      {
        if (ROGUE_COND(((RogueScanner__consume__Character( ROGUE_ARG(THIS->reader), (RogueCharacter)':' )))))
        {
          RogueJSONParser__consume_spaces_and_eols( ROGUE_ARG(THIS) );
          ROGUE_DEF_LOCAL_REF(RogueClassValue*,value_6,(((RogueClassValue*)(RogueJSONParser__parse_value( ROGUE_ARG(THIS) )))));
          RogueValueTable__set__String_Value( table_2, key_5, value_6 );
        }
        else
        {
          RogueValueTable__set__String_Value( table_2, key_5, ROGUE_ARG((RogueValue__create__Logical( true ))) );
        }
        RogueJSONParser__consume_spaces_and_eols( ROGUE_ARG(THIS) );
      }
    }
  }
  if (ROGUE_COND(!(((RogueScanner__consume__Character( ROGUE_ARG(THIS->reader), close_ch_1 ))))))
  {
    throw ((RogueClassJSONParseError*)(RogueJSONParseError___throw( ROGUE_ARG(((RogueClassJSONParseError*)(RogueJSONParseError__init__String( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassJSONParseError*,ROGUE_CREATE_OBJECT(JSONParseError))), ROGUE_ARG(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[20] )))), ROGUE_ARG(((RogueString*)(RogueString__operatorPLUS__Character( ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], Rogue_literal_strings[0] ))), close_ch_1 )))) )))), Rogue_literal_strings[21] )))) )))) )))) )));
  }
  return (RogueClassValue*)(((RogueClassValue*)(table_2)));
}

RogueClassValue* RogueJSONParser__parse_list__Character_Character( RogueClassJSONParser* THIS, RogueCharacter open_ch_0, RogueCharacter close_ch_1 )
{
  ROGUE_GC_CHECK;
  RogueJSONParser__consume_spaces_and_eols( ROGUE_ARG(THIS) );
  if (ROGUE_COND(!(((RogueScanner__consume__Character( ROGUE_ARG(THIS->reader), open_ch_0 ))))))
  {
    return (RogueClassValue*)(((RogueClassValue*)(((RogueClassUndefinedValue*)ROGUE_SINGLETON(UndefinedValue)))));
  }
  RogueJSONParser__consume_spaces_and_eols( ROGUE_ARG(THIS) );
  ROGUE_DEF_LOCAL_REF(RogueClassValueList*,list_2,(((RogueClassValueList*)(RogueValueList__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassValueList*,ROGUE_CREATE_OBJECT(ValueList))) )))));
  if (ROGUE_COND(((RogueScanner__consume__Character( ROGUE_ARG(THIS->reader), close_ch_1 )))))
  {
    return (RogueClassValue*)(((RogueClassValue*)(list_2)));
  }
  RogueInt32 prev_pos_3 = (THIS->reader->position);
  RogueLogical first_4 = (true);
  while (ROGUE_COND(((((first_4) || (((RogueScanner__consume__Character( ROGUE_ARG(THIS->reader), (RogueCharacter)',' )))))) || (((((((RogueScanner__has_another( ROGUE_ARG(THIS->reader) )))) && (((((RogueScanner__peek( ROGUE_ARG(THIS->reader) )))) != (close_ch_1))))) && (((THIS->reader->position) > (prev_pos_3))))))))
  {
    ROGUE_GC_CHECK;
    first_4 = ((RogueLogical)(false));
    prev_pos_3 = ((RogueInt32)(THIS->reader->position));
    RogueJSONParser__consume_spaces_and_eols( ROGUE_ARG(THIS) );
    if (ROGUE_COND(((((RogueScanner__peek( ROGUE_ARG(THIS->reader) )))) == (close_ch_1))))
    {
      goto _auto_1260;
    }
    RogueValueList__add__Value( list_2, ROGUE_ARG(((RogueClassValue*)(RogueJSONParser__parse_value( ROGUE_ARG(THIS) )))) );
    RogueJSONParser__consume_spaces_and_eols( ROGUE_ARG(THIS) );
  }
  _auto_1260:;
  if (ROGUE_COND(!(((RogueScanner__consume__Character( ROGUE_ARG(THIS->reader), close_ch_1 ))))))
  {
    throw ((RogueClassJSONParseError*)(RogueJSONParseError___throw( ROGUE_ARG(((RogueClassJSONParseError*)(RogueJSONParseError__init__String( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassJSONParseError*,ROGUE_CREATE_OBJECT(JSONParseError))), ROGUE_ARG(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[20] )))), ROGUE_ARG(((RogueString*)(RogueString__operatorPLUS__Character( ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], Rogue_literal_strings[0] ))), close_ch_1 )))) )))), Rogue_literal_strings[21] )))) )))) )))) )));
  }
  return (RogueClassValue*)(((RogueClassValue*)(list_2)));
}

RogueString* RogueJSONParser__parse_string( RogueClassJSONParser* THIS )
{
  ROGUE_GC_CHECK;
  RogueJSONParser__consume_spaces_and_eols( ROGUE_ARG(THIS) );
  RogueCharacter terminator_0 = ((RogueCharacter)'"');
  if (ROGUE_COND(((RogueScanner__consume__Character( ROGUE_ARG(THIS->reader), (RogueCharacter)'"' )))))
  {
    terminator_0 = ((RogueCharacter)((RogueCharacter)'"'));
  }
  else if (ROGUE_COND(((RogueScanner__consume__Character( ROGUE_ARG(THIS->reader), (RogueCharacter)'\'' )))))
  {
    terminator_0 = ((RogueCharacter)((RogueCharacter)'\''));
  }
  if (ROGUE_COND(!(((RogueScanner__has_another( ROGUE_ARG(THIS->reader) ))))))
  {
    return (RogueString*)(Rogue_literal_strings[0]);
  }
  ROGUE_DEF_LOCAL_REF(RogueClassJSONParserBuffer*,buffer_1,(((RogueClassJSONParserBuffer*)ROGUE_SINGLETON(JSONParserBuffer))));
  RogueStringBuilder__clear( ((RogueStringBuilder*)buffer_1) );
  RogueCharacter ch_2 = (((RogueScanner__read( ROGUE_ARG(THIS->reader) ))));
  while (ROGUE_COND(((((RogueScanner__has_another( ROGUE_ARG(THIS->reader) )))) && (((ch_2) != (terminator_0))))))
  {
    ROGUE_GC_CHECK;
    if (ROGUE_COND(((ch_2) == ((RogueCharacter)'\\'))))
    {
      ch_2 = ((RogueCharacter)(((RogueScanner__read( ROGUE_ARG(THIS->reader) )))));
      if (ROGUE_COND(((ch_2) == ((RogueCharacter)'b'))))
      {
        RogueStringBuilder__print__Character_Logical( ((RogueStringBuilder*)buffer_1), (RogueCharacter)8, true );
      }
      else if (ROGUE_COND(((ch_2) == ((RogueCharacter)'f'))))
      {
        RogueStringBuilder__print__Character_Logical( ((RogueStringBuilder*)buffer_1), (RogueCharacter)12, true );
      }
      else if (ROGUE_COND(((ch_2) == ((RogueCharacter)'n'))))
      {
        RogueStringBuilder__print__Character_Logical( ((RogueStringBuilder*)buffer_1), (RogueCharacter)10, true );
      }
      else if (ROGUE_COND(((ch_2) == ((RogueCharacter)'r'))))
      {
        RogueStringBuilder__print__Character_Logical( ((RogueStringBuilder*)buffer_1), (RogueCharacter)13, true );
      }
      else if (ROGUE_COND(((ch_2) == ((RogueCharacter)'t'))))
      {
        RogueStringBuilder__print__Character_Logical( ((RogueStringBuilder*)buffer_1), (RogueCharacter)9, true );
      }
      else if (ROGUE_COND(((ch_2) == ((RogueCharacter)'u'))))
      {
        RogueStringBuilder__print__Character_Logical( ((RogueStringBuilder*)buffer_1), ROGUE_ARG(((RogueJSONParser__parse_hex_quad( ROGUE_ARG(THIS) )))), true );
      }
      else
      {
        RogueStringBuilder__print__Character_Logical( ((RogueStringBuilder*)buffer_1), ch_2, true );
      }
    }
    else
    {
      RogueStringBuilder__print__Character_Logical( ((RogueStringBuilder*)buffer_1), ch_2, true );
    }
    ch_2 = ((RogueCharacter)(((RogueScanner__read( ROGUE_ARG(THIS->reader) )))));
  }
  return (RogueString*)(((RogueString*)(RogueString__consolidated( ROGUE_ARG(((RogueString*)(RogueStringBuilder__to_String( ((RogueStringBuilder*)buffer_1) )))) ))));
}

RogueCharacter RogueJSONParser__parse_hex_quad( RogueClassJSONParser* THIS )
{
  ROGUE_GC_CHECK;
  RogueInt32 code_0 = (0);
  {
    RogueInt32 i_1 = (1);
    RogueInt32 _auto_256_2 = (4);
    for (;ROGUE_COND(((i_1) <= (_auto_256_2)));++i_1)
    {
      ROGUE_GC_CHECK;
      if (ROGUE_COND(((RogueScanner__has_another( ROGUE_ARG(THIS->reader) )))))
      {
        code_0 = ((RogueInt32)(((((code_0) << (4))) | (((RogueCharacter__to_number__Int32( ROGUE_ARG(((RogueScanner__read( ROGUE_ARG(THIS->reader) )))), 10 )))))));
      }
    }
  }
  return (RogueCharacter)(((RogueCharacter)(code_0)));
}

RogueString* RogueJSONParser__parse_identifier( RogueClassJSONParser* THIS )
{
  ROGUE_GC_CHECK;
  RogueJSONParser__consume_spaces_and_eols( ROGUE_ARG(THIS) );
  RogueCharacter ch_0 = (((RogueScanner__peek( ROGUE_ARG(THIS->reader) ))));
  if (ROGUE_COND(((((ch_0) == ((RogueCharacter)'"'))) || (((ch_0) == ((RogueCharacter)'\''))))))
  {
    return (RogueString*)(((RogueString*)(RogueJSONParser__parse_string( ROGUE_ARG(THIS) ))));
  }
  else
  {
    ROGUE_DEF_LOCAL_REF(RogueClassJSONParserBuffer*,buffer_1,(((RogueClassJSONParserBuffer*)ROGUE_SINGLETON(JSONParserBuffer))));
    RogueStringBuilder__clear( ((RogueStringBuilder*)buffer_1) );
    RogueLogical finished_2 = (false);
    while (ROGUE_COND(((!(finished_2)) && (((RogueScanner__has_another( ROGUE_ARG(THIS->reader) )))))))
    {
      ROGUE_GC_CHECK;
      if (ROGUE_COND(((RogueCharacter__is_identifier__Logical_Logical( ch_0, false, true )))))
      {
        RogueScanner__read( ROGUE_ARG(THIS->reader) );
        RogueStringBuilder__print__Character_Logical( ((RogueStringBuilder*)buffer_1), ch_0, true );
        ch_0 = ((RogueCharacter)(((RogueScanner__peek( ROGUE_ARG(THIS->reader) )))));
      }
      else
      {
        finished_2 = ((RogueLogical)(true));
      }
    }
    if (ROGUE_COND(((buffer_1->count) == (0))))
    {
      throw ((RogueClassJSONParseError*)(RogueJSONParseError___throw( ROGUE_ARG(((RogueClassJSONParseError*)(RogueJSONParseError__init__String( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassJSONParseError*,ROGUE_CREATE_OBJECT(JSONParseError))), Rogue_literal_strings[19] )))) )));
    }
    return (RogueString*)(((RogueString*)(RogueString__consolidated( ROGUE_ARG(((RogueString*)(RogueStringBuilder__to_String( ((RogueStringBuilder*)buffer_1) )))) ))));
  }
}

RogueCharacter RogueJSONParser__peek( RogueClassJSONParser* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueCharacter)(((RogueScanner__peek( ROGUE_ARG(THIS->reader) ))));
}

RogueLogical RogueJSONParser__next_is_identifier( RogueClassJSONParser* THIS )
{
  ROGUE_GC_CHECK;
  RogueCharacter ch_0 = (((RogueScanner__peek( ROGUE_ARG(THIS->reader) ))));
  if (ROGUE_COND(((RogueCharacter__is_identifier__Logical_Logical( ch_0, true, true )))))
  {
    return (RogueLogical)(true);
  }
  return (RogueLogical)(((((ch_0) == ((RogueCharacter)'"'))) || (((ch_0) == ((RogueCharacter)'\'')))));
}

RogueClassValue* RogueJSONParser__parse_number( RogueClassJSONParser* THIS )
{
  ROGUE_GC_CHECK;
  RogueJSONParser__consume_spaces_and_eols( ROGUE_ARG(THIS) );
  RogueReal64 sign_0 = (1.0);
  if (ROGUE_COND(((RogueScanner__consume__Character( ROGUE_ARG(THIS->reader), (RogueCharacter)'-' )))))
  {
    sign_0 = ((RogueReal64)(-1.0));
    RogueJSONParser__consume_spaces_and_eols( ROGUE_ARG(THIS) );
  }
  RogueReal64 n_1 = (0.0);
  RogueCharacter ch_2 = (((RogueScanner__peek( ROGUE_ARG(THIS->reader) ))));
  while (ROGUE_COND(((((((RogueScanner__has_another( ROGUE_ARG(THIS->reader) )))) && (((ch_2) >= ((RogueCharacter)'0'))))) && (((ch_2) <= ((RogueCharacter)'9'))))))
  {
    ROGUE_GC_CHECK;
    RogueScanner__read( ROGUE_ARG(THIS->reader) );
    n_1 = ((RogueReal64)(((((n_1) * (10.0))) + (((RogueReal64)(((ch_2) - ((RogueCharacter)'0'))))))));
    ch_2 = ((RogueCharacter)(((RogueScanner__peek( ROGUE_ARG(THIS->reader) )))));
  }
  if (ROGUE_COND(((RogueScanner__consume__Character( ROGUE_ARG(THIS->reader), (RogueCharacter)'.' )))))
  {
    RogueReal64 decimal_3 = (0.0);
    RogueReal64 power_4 = (0.0);
    ch_2 = ((RogueCharacter)(((RogueScanner__peek( ROGUE_ARG(THIS->reader) )))));
    while (ROGUE_COND(((((((RogueScanner__has_another( ROGUE_ARG(THIS->reader) )))) && (((ch_2) >= ((RogueCharacter)'0'))))) && (((ch_2) <= ((RogueCharacter)'9'))))))
    {
      ROGUE_GC_CHECK;
      RogueScanner__read( ROGUE_ARG(THIS->reader) );
      decimal_3 = ((RogueReal64)(((((decimal_3) * (10.0))) + (((RogueReal64)(((ch_2) - ((RogueCharacter)'0'))))))));
      power_4 += 1.0;
      ch_2 = ((RogueCharacter)(((RogueScanner__peek( ROGUE_ARG(THIS->reader) )))));
    }
    n_1 += ((decimal_3) / (((RogueReal64) pow((double)10.0, (double)power_4))));
  }
  if (ROGUE_COND(((((RogueScanner__consume__Character( ROGUE_ARG(THIS->reader), (RogueCharacter)'e' )))) || (((RogueScanner__consume__Character( ROGUE_ARG(THIS->reader), (RogueCharacter)'E' )))))))
  {
    RogueLogical negexp_5 = (false);
    if (ROGUE_COND(((!(((RogueScanner__consume__Character( ROGUE_ARG(THIS->reader), (RogueCharacter)'+' ))))) && (((RogueScanner__consume__Character( ROGUE_ARG(THIS->reader), (RogueCharacter)'-' )))))))
    {
      negexp_5 = ((RogueLogical)(true));
    }
    RogueReal64 power_6 = (0.0);
    ch_2 = ((RogueCharacter)(((RogueScanner__peek( ROGUE_ARG(THIS->reader) )))));
    while (ROGUE_COND(((((((RogueScanner__has_another( ROGUE_ARG(THIS->reader) )))) && (((ch_2) >= ((RogueCharacter)'0'))))) && (((ch_2) <= ((RogueCharacter)'9'))))))
    {
      ROGUE_GC_CHECK;
      RogueScanner__read( ROGUE_ARG(THIS->reader) );
      power_6 = ((RogueReal64)(((((power_6) * (10.0))) + (((RogueReal64)(((ch_2) - ((RogueCharacter)'0'))))))));
      ch_2 = ((RogueCharacter)(((RogueScanner__peek( ROGUE_ARG(THIS->reader) )))));
    }
    if (ROGUE_COND(negexp_5))
    {
      n_1 /= ((RogueReal64) pow((double)10.0, (double)power_6));
    }
    else
    {
      n_1 *= ((RogueReal64) pow((double)10.0, (double)power_6));
    }
  }
  n_1 = ((RogueReal64)(((n_1) * (sign_0))));
  return (RogueClassValue*)(((RogueClassValue*)(((RogueClassReal64Value*)(RogueReal64Value__init__Real64( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassReal64Value*,ROGUE_CREATE_OBJECT(Real64Value))), n_1 ))))));
}

RogueCharacter RogueJSONParser__read( RogueClassJSONParser* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueCharacter)(((RogueScanner__read( ROGUE_ARG(THIS->reader) ))));
}

void RogueJSONParser__consume_spaces( RogueClassJSONParser* THIS )
{
  ROGUE_GC_CHECK;
  while (ROGUE_COND(((RogueScanner__consume_spaces( ROGUE_ARG(THIS->reader) )))))
  {
    ROGUE_GC_CHECK;
  }
}

void RogueJSONParser__consume_spaces_and_eols( RogueClassJSONParser* THIS )
{
  ROGUE_GC_CHECK;
  while (ROGUE_COND(((((RogueScanner__consume_spaces( ROGUE_ARG(THIS->reader) )))) || (((RogueScanner__consume_eols( ROGUE_ARG(THIS->reader) )))))))
  {
    ROGUE_GC_CHECK;
  }
}

RogueClassJSON* RogueJSON__init_object( RogueClassJSON* THIS )
{
  ROGUE_GC_CHECK;
  RogueObject__init_object( ROGUE_ARG(((RogueObject*)THIS)) );
  return (RogueClassJSON*)(THIS);
}

RogueString* RogueJSON__type_name( RogueClassJSON* THIS )
{
  return (RogueString*)(Rogue_literal_strings[152]);
}

RogueClass_Function_String_RETURNSString_* Rogue_Function_String_RETURNSString___init_object( RogueClass_Function_String_RETURNSString_* THIS )
{
  ROGUE_GC_CHECK;
  RogueObject__init_object( ROGUE_ARG(((RogueObject*)THIS)) );
  return (RogueClass_Function_String_RETURNSString_*)(THIS);
}

RogueString* Rogue_Function_String_RETURNSString___type_name( RogueClass_Function_String_RETURNSString_* THIS )
{
  return (RogueString*)(Rogue_literal_strings[154]);
}

RogueString* Rogue_Function_String_RETURNSString___call__String( RogueClass_Function_String_RETURNSString_* THIS, RogueString* param1_0 )
{
  return (RogueString*)(((RogueString*)(NULL)));
}

RogueClassFunction_283* RogueFunction_283__init_object( RogueClassFunction_283* THIS )
{
  ROGUE_GC_CHECK;
  Rogue_Function_String_RETURNSString___init_object( ROGUE_ARG(((RogueClass_Function_String_RETURNSString_*)THIS)) );
  return (RogueClassFunction_283*)(THIS);
}

RogueString* RogueFunction_283__type_name( RogueClassFunction_283* THIS )
{
  return (RogueString*)(Rogue_literal_strings[207]);
}

RogueString* RogueFunction_283__call__String( RogueClassFunction_283* THIS, RogueString* arg_0 )
{
  return (RogueString*)(((RogueString*)(RogueGlobal__prep_arg__String( ((RogueClassGlobal*)ROGUE_SINGLETON(Global)), arg_0 ))));
}

RogueClassRuntime* RogueRuntime__init_object( RogueClassRuntime* THIS )
{
  ROGUE_GC_CHECK;
  RogueObject__init_object( ROGUE_ARG(((RogueObject*)THIS)) );
  return (RogueClassRuntime*)(THIS);
}

RogueString* RogueRuntime__type_name( RogueClassRuntime* THIS )
{
  return (RogueString*)(Rogue_literal_strings[155]);
}

RogueWeakReference* RogueWeakReference__init_object( RogueWeakReference* THIS )
{
  ROGUE_GC_CHECK;
  RogueObject__init_object( ROGUE_ARG(((RogueObject*)THIS)) );
  return (RogueWeakReference*)(THIS);
}

RogueString* RogueWeakReference__type_name( RogueWeakReference* THIS )
{
  return (RogueString*)(Rogue_literal_strings[157]);
}

void RogueWeakReference__on_cleanup( RogueWeakReference* THIS )
{
  ROGUE_GC_CHECK;
  if (Rogue_weak_references == THIS)
  {
    Rogue_weak_references = THIS->next_weak_reference;
  }
  else
  {
    RogueWeakReference* cur = Rogue_weak_references;
    while (cur && cur->next_weak_reference != THIS)
    {
      cur = cur->next_weak_reference;
    }
    if (cur) cur->next_weak_reference = cur->next_weak_reference->next_weak_reference;
  }

}

RogueClassPrintWriterAdapter* RoguePrintWriterAdapter__init_object( RogueClassPrintWriterAdapter* THIS )
{
  ROGUE_GC_CHECK;
  RogueObject__init_object( ROGUE_ARG(((RogueObject*)THIS)) );
  THIS->buffer = ((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )));
  return (RogueClassPrintWriterAdapter*)(THIS);
}

RogueString* RoguePrintWriterAdapter__type_name( RogueClassPrintWriterAdapter* THIS )
{
  return (RogueString*)(Rogue_literal_strings[162]);
}

RogueClassPrintWriterAdapter* RoguePrintWriterAdapter__close( RogueClassPrintWriterAdapter* THIS )
{
  ROGUE_GC_CHECK;
  RogueWriter_Byte___close( ROGUE_ARG(((((RogueObject*)THIS->output)))) );
  return (RogueClassPrintWriterAdapter*)(THIS);
}

RogueClassPrintWriterAdapter* RoguePrintWriterAdapter__flush( RogueClassPrintWriterAdapter* THIS )
{
  ROGUE_GC_CHECK;
  RoguePrintWriterAdapter__write__StringBuilder( ROGUE_ARG(THIS), ROGUE_ARG(THIS->buffer) );
  RogueStringBuilder__clear( ROGUE_ARG(THIS->buffer) );
  RogueWriter_Byte___flush( ROGUE_ARG(((((RogueObject*)THIS->output)))) );
  return (RogueClassPrintWriterAdapter*)(THIS);
}

RogueClassPrintWriterAdapter* RoguePrintWriterAdapter__print__String( RogueClassPrintWriterAdapter* THIS, RogueString* value_0 )
{
  ROGUE_GC_CHECK;
  RogueStringBuilder__print__String( ROGUE_ARG(THIS->buffer), value_0 );
  if (ROGUE_COND(((THIS->buffer->count) > (1024))))
  {
    RoguePrintWriterAdapter__flush( ROGUE_ARG(THIS) );
  }
  return (RogueClassPrintWriterAdapter*)(THIS);
}

RogueClassPrintWriterAdapter* RoguePrintWriterAdapter__println( RogueClassPrintWriterAdapter* THIS )
{
  ROGUE_GC_CHECK;
  RogueStringBuilder__print__Character_Logical( ROGUE_ARG(THIS->buffer), (RogueCharacter)10, true );
  return (RogueClassPrintWriterAdapter*)(((RogueClassPrintWriterAdapter*)(RoguePrintWriterAdapter__flush( ROGUE_ARG(THIS) ))));
}

RogueClassPrintWriterAdapter* RoguePrintWriterAdapter__println__String( RogueClassPrintWriterAdapter* THIS, RogueString* value_0 )
{
  ROGUE_GC_CHECK;
  return (RogueClassPrintWriterAdapter*)(((RogueClassPrintWriterAdapter*)(RoguePrintWriterAdapter__println( ROGUE_ARG(((RogueClassPrintWriterAdapter*)(RoguePrintWriterAdapter__print__String( ROGUE_ARG(THIS), value_0 )))) ))));
}

RogueClassPrintWriterAdapter* RoguePrintWriterAdapter__write__StringBuilder( RogueClassPrintWriterAdapter* THIS, RogueStringBuilder* _buffer_0 )
{
  ROGUE_GC_CHECK;
  RogueWriter_Byte___write__Byte_List( ROGUE_ARG(((((RogueObject*)THIS->output)))), ROGUE_ARG(_buffer_0->utf8) );
  return (RogueClassPrintWriterAdapter*)(THIS);
}

RogueClassPrintWriterAdapter* RoguePrintWriterAdapter__init__Writer_Byte_( RogueClassPrintWriterAdapter* THIS, RogueClassWriter_Byte_* _auto_327_0 )
{
  ROGUE_GC_CHECK;
  THIS->output = _auto_327_0;
  return (RogueClassPrintWriterAdapter*)(THIS);
}

RogueClassPrintWriter_buffer_* RoguePrintWriter_buffer___close( RogueObject* THIS )
{
  switch (THIS->type->index)
  {
    case 65:
      return (RogueClassPrintWriter_buffer_*)RoguePrintWriterAdapter__close( (RogueClassPrintWriterAdapter*)THIS );
    default:
      return 0;
  }
}

RogueClassPrintWriter_buffer_* RoguePrintWriter_buffer___flush( RogueObject* THIS )
{
  switch (THIS->type->index)
  {
    case 65:
      return (RogueClassPrintWriter_buffer_*)RoguePrintWriterAdapter__flush( (RogueClassPrintWriterAdapter*)THIS );
    default:
      return 0;
  }
}

RogueClassPrintWriter_buffer_* RoguePrintWriter_buffer___println__String( RogueObject* THIS, RogueString* value_0 )
{
  switch (THIS->type->index)
  {
    case 65:
      return (RogueClassPrintWriter_buffer_*)RoguePrintWriterAdapter__println__String( (RogueClassPrintWriterAdapter*)THIS, value_0 );
    default:
      return 0;
  }
}

RogueClassPrintWriter_buffer_* RoguePrintWriter_buffer___write__StringBuilder( RogueObject* THIS, RogueStringBuilder* buffer_0 )
{
  switch (THIS->type->index)
  {
    case 65:
      return (RogueClassPrintWriter_buffer_*)RoguePrintWriterAdapter__write__StringBuilder( (RogueClassPrintWriterAdapter*)THIS, buffer_0 );
    default:
      return 0;
  }
}

RogueClassLogicalValue* RogueLogicalValue__init_object( RogueClassLogicalValue* THIS )
{
  ROGUE_GC_CHECK;
  RogueValue__init_object( ROGUE_ARG(((RogueClassValue*)THIS)) );
  return (RogueClassLogicalValue*)(THIS);
}

RogueString* RogueLogicalValue__to_String( RogueClassLogicalValue* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(((RogueString*)(RogueLogical__to_String( ROGUE_ARG(THIS->value) ))));
}

RogueString* RogueLogicalValue__type_name( RogueClassLogicalValue* THIS )
{
  return (RogueString*)(Rogue_literal_strings[168]);
}

RogueLogical RogueLogicalValue__is_logical( RogueClassLogicalValue* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueLogical)(true);
}

RogueInt64 RogueLogicalValue__to_Int64( RogueClassLogicalValue* THIS )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(THIS->value))
  {
    return (RogueInt64)(1LL);
  }
  else
  {
    return (RogueInt64)(0LL);
  }
}

RogueLogical RogueLogicalValue__to_Logical( RogueClassLogicalValue* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueLogical)(THIS->value);
}

RogueStringBuilder* RogueLogicalValue__to_json__StringBuilder_Int32( RogueClassLogicalValue* THIS, RogueStringBuilder* buffer_0, RogueInt32 flags_1 )
{
  ROGUE_GC_CHECK;
  RogueStringBuilder__print__Logical( buffer_0, ROGUE_ARG(THIS->value) );
  return (RogueStringBuilder*)(buffer_0);
}

RogueClassLogicalValue* RogueLogicalValue__init__Logical( RogueClassLogicalValue* THIS, RogueLogical _auto_328_0 )
{
  ROGUE_GC_CHECK;
  THIS->value = _auto_328_0;
  return (RogueClassLogicalValue*)(THIS);
}

RogueClassReal64Value* RogueReal64Value__init_object( RogueClassReal64Value* THIS )
{
  ROGUE_GC_CHECK;
  RogueValue__init_object( ROGUE_ARG(((RogueClassValue*)THIS)) );
  return (RogueClassReal64Value*)(THIS);
}

RogueString* RogueReal64Value__to_String( RogueClassReal64Value* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(((RogueString*)(RogueReal64__format__OptionalInt32( ROGUE_ARG(THIS->value), (RogueOptionalInt32__create()) ))));
}

RogueString* RogueReal64Value__type_name( RogueClassReal64Value* THIS )
{
  return (RogueString*)(Rogue_literal_strings[169]);
}

RogueLogical RogueReal64Value__is_number( RogueClassReal64Value* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueLogical)(true);
}

RogueLogical RogueReal64Value__operatorEQUALSEQUALS__Value( RogueClassReal64Value* THIS, RogueClassValue* other_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND((Rogue_call_ROGUEM6( 46, other_0 ))))
  {
    return (RogueLogical)(((THIS->value) == ((Rogue_call_ROGUEM9( 109, other_0 )))));
  }
  if (ROGUE_COND((Rogue_call_ROGUEM6( 43, other_0 ))))
  {
    return (RogueLogical)(((((THIS->value) != (0.0))) == ((Rogue_call_ROGUEM6( 108, other_0 )))));
  }
  else
  {
    return (RogueLogical)(false);
  }
}

RogueInt64 RogueReal64Value__to_Int64( RogueClassReal64Value* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueInt64)(((RogueInt64)(THIS->value)));
}

RogueReal64 RogueReal64Value__to_Real64( RogueClassReal64Value* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueReal64)(THIS->value);
}

RogueStringBuilder* RogueReal64Value__to_json__StringBuilder_Int32( RogueClassReal64Value* THIS, RogueStringBuilder* buffer_0, RogueInt32 flags_1 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(!!(((RogueReal64__fractional_part( ROGUE_ARG(THIS->value) ))))))
  {
    RogueStringBuilder__print__Real64( buffer_0, ROGUE_ARG(THIS->value) );
  }
  else
  {
    RogueStringBuilder__print__Real64_Int32( buffer_0, ROGUE_ARG(THIS->value), 0 );
  }
  return (RogueStringBuilder*)(buffer_0);
}

RogueClassReal64Value* RogueReal64Value__init__Real64( RogueClassReal64Value* THIS, RogueReal64 _auto_331_0 )
{
  ROGUE_GC_CHECK;
  THIS->value = _auto_331_0;
  return (RogueClassReal64Value*)(THIS);
}

RogueClassNullValue* RogueNullValue__init_object( RogueClassNullValue* THIS )
{
  ROGUE_GC_CHECK;
  RogueValue__init_object( ROGUE_ARG(((RogueClassValue*)THIS)) );
  return (RogueClassNullValue*)(THIS);
}

RogueString* RogueNullValue__to_String( RogueClassNullValue* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(Rogue_literal_strings[1]);
}

RogueString* RogueNullValue__type_name( RogueClassNullValue* THIS )
{
  return (RogueString*)(Rogue_literal_strings[170]);
}

RogueLogical RogueNullValue__is_null( RogueClassNullValue* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueLogical)(true);
}

RogueLogical RogueNullValue__is_non_null( RogueClassNullValue* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueLogical)(false);
}

RogueLogical RogueNullValue__operatorEQUALSEQUALS__Value( RogueClassNullValue* THIS, RogueClassValue* other_0 )
{
  ROGUE_GC_CHECK;
  return (RogueLogical)(((((void*)other_0) == ((void*)NULL)) || ((Rogue_call_ROGUEM6( 44, other_0 )))));
}

RogueInt64 RogueNullValue__to_Int64( RogueClassNullValue* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueInt64)(0LL);
}

RogueStringBuilder* RogueNullValue__to_json__StringBuilder_Int32( RogueClassNullValue* THIS, RogueStringBuilder* buffer_0, RogueInt32 flags_1 )
{
  ROGUE_GC_CHECK;
  return (RogueStringBuilder*)(((RogueStringBuilder*)(RogueStringBuilder__print__String( buffer_0, Rogue_literal_strings[1] ))));
}

RogueClassStringValue* RogueStringValue__init_object( RogueClassStringValue* THIS )
{
  ROGUE_GC_CHECK;
  RogueValue__init_object( ROGUE_ARG(((RogueClassValue*)THIS)) );
  return (RogueClassStringValue*)(THIS);
}

RogueString* RogueStringValue__to_String( RogueClassStringValue* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(THIS->value);
}

RogueString* RogueStringValue__type_name( RogueClassStringValue* THIS )
{
  return (RogueString*)(Rogue_literal_strings[171]);
}

RogueInt32 RogueStringValue__count( RogueClassStringValue* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueInt32)(THIS->value->character_count);
}

RogueClassValue* RogueStringValue__get__Int32( RogueClassStringValue* THIS, RogueInt32 index_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((((index_0) < (0))) || (((index_0) >= (THIS->value->character_count))))))
  {
    return (RogueClassValue*)(((RogueClassValue*)(((RogueClassUndefinedValue*)ROGUE_SINGLETON(UndefinedValue)))));
  }
  return (RogueClassValue*)((RogueValue__create__String( ROGUE_ARG(((RogueString*)(RogueString__operatorPLUS__Character( ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], Rogue_literal_strings[0] ))), ROGUE_ARG(RogueString_character_at(THIS->value,index_0)) )))) )));
}

RogueLogical RogueStringValue__is_string( RogueClassStringValue* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueLogical)(true);
}

RogueLogical RogueStringValue__operatorEQUALSEQUALS__Value( RogueClassStringValue* THIS, RogueClassValue* other_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND((Rogue_call_ROGUEM6( 49, other_0 ))))
  {
    return (RogueLogical)((RogueString__operatorEQUALSEQUALS__String_String( ROGUE_ARG(THIS->value), ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ((RogueObject*)other_0) ))) )));
  }
  else
  {
    return (RogueLogical)(false);
  }
}

RogueInt64 RogueStringValue__to_Int64( RogueClassStringValue* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueInt64)((RogueInt64)strtoll( (char*)THIS->value->utf8, 0, 10 ));
}

RogueLogical RogueStringValue__to_Logical( RogueClassStringValue* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueLogical)((((((((((RogueString__operatorEQUALSEQUALS__String_String( ROGUE_ARG(THIS->value), Rogue_literal_strings[22] ))) || ((RogueString__operatorEQUALSEQUALS__String_String( ROGUE_ARG(THIS->value), Rogue_literal_strings[172] ))))) || ((RogueString__operatorEQUALSEQUALS__String_String( ROGUE_ARG(THIS->value), Rogue_literal_strings[173] ))))) || ((RogueString__operatorEQUALSEQUALS__String_String( ROGUE_ARG(THIS->value), Rogue_literal_strings[174] ))))) || ((RogueString__operatorEQUALSEQUALS__String_String( ROGUE_ARG(THIS->value), Rogue_literal_strings[175] )))));
}

RogueReal64 RogueStringValue__to_Real64( RogueClassStringValue* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueReal64)(strtod( (char*)THIS->value->utf8, 0 ));
}

RogueStringBuilder* RogueStringValue__to_json__StringBuilder_Int32( RogueClassStringValue* THIS, RogueStringBuilder* buffer_0, RogueInt32 flags_1 )
{
  ROGUE_GC_CHECK;
  return (RogueStringBuilder*)((RogueStringValue__to_json__String_StringBuilder_Int32( ROGUE_ARG(THIS->value), buffer_0, flags_1 )));
}

RogueClassStringValue* RogueStringValue__init__String( RogueClassStringValue* THIS, RogueString* _auto_333_0 )
{
  ROGUE_GC_CHECK;
  THIS->value = _auto_333_0;
  return (RogueClassStringValue*)(THIS);
}

RogueClassUndefinedValue* RogueUndefinedValue__init_object( RogueClassUndefinedValue* THIS )
{
  ROGUE_GC_CHECK;
  RogueNullValue__init_object( ROGUE_ARG(((RogueClassNullValue*)THIS)) );
  return (RogueClassUndefinedValue*)(THIS);
}

RogueString* RogueUndefinedValue__to_String( RogueClassUndefinedValue* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(Rogue_literal_strings[0]);
}

RogueString* RogueUndefinedValue__type_name( RogueClassUndefinedValue* THIS )
{
  return (RogueString*)(Rogue_literal_strings[208]);
}

RogueClassOutOfBoundsError* RogueOutOfBoundsError__init_object( RogueClassOutOfBoundsError* THIS )
{
  ROGUE_GC_CHECK;
  RogueError__init_object( ROGUE_ARG(((RogueClassError*)THIS)) );
  return (RogueClassOutOfBoundsError*)(THIS);
}

RogueString* RogueOutOfBoundsError__type_name( RogueClassOutOfBoundsError* THIS )
{
  return (RogueString*)(Rogue_literal_strings[204]);
}

RogueClassOutOfBoundsError* RogueOutOfBoundsError__init__String( RogueClassOutOfBoundsError* THIS, RogueString* _auto_446_0 )
{
  ROGUE_GC_CHECK;
  THIS->message = _auto_446_0;
  return (RogueClassOutOfBoundsError*)(THIS);
}

RogueClassOutOfBoundsError* RogueOutOfBoundsError___throw( RogueClassOutOfBoundsError* THIS )
{
  ROGUE_GC_CHECK;
  throw THIS;

}

RogueClassOutOfBoundsError* RogueOutOfBoundsError__init__Int32_Int32( RogueClassOutOfBoundsError* THIS, RogueInt32 index_0, RogueInt32 limit_1 )
{
  ROGUE_GC_CHECK;
  switch (limit_1)
  {
    case 0:
    {
      RogueOutOfBoundsError__init__String( ROGUE_ARG(THIS), ROGUE_ARG(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[34] )))), ROGUE_ARG(((RogueString*)(RogueString__operatorPLUS__Int32( ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], Rogue_literal_strings[0] ))), index_0 )))) )))), Rogue_literal_strings[36] )))) )))) );
      break;
    }
    case 1:
    {
      RogueOutOfBoundsError__init__String( ROGUE_ARG(THIS), ROGUE_ARG(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[34] )))), ROGUE_ARG(((RogueString*)(RogueString__operatorPLUS__Int32( ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], Rogue_literal_strings[0] ))), index_0 )))) )))), Rogue_literal_strings[37] )))) )))) );
      break;
    }
    default:
    {
      RogueOutOfBoundsError__init__String( ROGUE_ARG(THIS), ROGUE_ARG(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[34] )))), ROGUE_ARG(((RogueString*)(RogueString__operatorPLUS__Int32( ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], Rogue_literal_strings[0] ))), index_0 )))) )))), Rogue_literal_strings[38] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], ROGUE_ARG(((RogueString*)(RogueString__pluralized__Int32( Rogue_literal_strings[39], limit_1 )))) ))) )))), Rogue_literal_strings[43] )))), ROGUE_ARG(((RogueString*)(RogueString__operatorPLUS__Int32( ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], Rogue_literal_strings[0] ))), 0 )))) )))), Rogue_literal_strings[44] )))), ROGUE_ARG(((RogueString*)(RogueString__operatorPLUS__Int32( ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], Rogue_literal_strings[0] ))), ROGUE_ARG(((limit_1) - (1))) )))) )))), Rogue_literal_strings[45] )))) )))) );
    }
  }
  return (RogueClassOutOfBoundsError*)(THIS);
}

RogueClassListRewriter_Character_* RogueListRewriter_Character___init_object( RogueClassListRewriter_Character_* THIS )
{
  ROGUE_GC_CHECK;
  RogueObject__init_object( ROGUE_ARG(((RogueObject*)THIS)) );
  return (RogueClassListRewriter_Character_*)(THIS);
}

RogueString* RogueListRewriter_Character___type_name( RogueClassListRewriter_Character_* THIS )
{
  return (RogueString*)(Rogue_literal_strings[163]);
}

RogueClassListRewriter_Character_* RogueListRewriter_Character___init__Character_List( RogueClassListRewriter_Character_* THIS, RogueCharacter_List* _auto_970_0 )
{
  ROGUE_GC_CHECK;
  THIS->list = _auto_970_0;
  return (RogueClassListRewriter_Character_*)(THIS);
}

RogueLogical RogueListRewriter_Character___has_another( RogueClassListRewriter_Character_* THIS )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((THIS->read_index) < (THIS->list->count))))
  {
    return (RogueLogical)(true);
  }
  else
  {
    RogueCharacter_List__discard_from__Int32( ROGUE_ARG(THIS->list), ROGUE_ARG(THIS->write_index) );
    return (RogueLogical)(false);
  }
  return (RogueLogical)(((THIS->read_index) < (THIS->list->count)));
}

RogueCharacter RogueListRewriter_Character___read( RogueClassListRewriter_Character_* THIS )
{
  ROGUE_GC_CHECK;
  ++THIS->read_index;
  return (RogueCharacter)(THIS->list->data->as_characters[((THIS->read_index) - (1))]);
}

RogueClassListRewriter_Character_* RogueListRewriter_Character___write__Character( RogueClassListRewriter_Character_* THIS, RogueCharacter value_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((THIS->write_index) == (THIS->read_index))))
  {
    RogueCharacter_List__reserve__Int32( ROGUE_ARG(THIS->list), 1 );
    RogueInt32 unread_count_1 = (((THIS->list->count) - (THIS->read_index)));
    RogueArray_set(THIS->list->data,((((RogueCharacter_List__capacity( ROGUE_ARG(THIS->list) )))) - (unread_count_1)),((RogueArray*)(THIS->list->data)),THIS->read_index,unread_count_1);
    THIS->read_index += ((((RogueCharacter_List__capacity( ROGUE_ARG(THIS->list) )))) - (THIS->list->count));
    THIS->list->count = ((RogueCharacter_List__capacity( ROGUE_ARG(THIS->list) )));
  }
  if (ROGUE_COND(((THIS->write_index) == (THIS->list->count))))
  {
    RogueCharacter_List__add__Character( ROGUE_ARG(THIS->list), value_0 );
  }
  else
  {
    THIS->list->data->as_characters[THIS->write_index] = (value_0);
  }
  ++THIS->write_index;
  return (RogueClassListRewriter_Character_*)(THIS);
}

RogueClassFunction_1184* RogueFunction_1184__init_object( RogueClassFunction_1184* THIS )
{
  ROGUE_GC_CHECK;
  Rogue_Function_____init_object( ROGUE_ARG(((RogueClass_Function___*)THIS)) );
  return (RogueClassFunction_1184*)(THIS);
}

RogueString* RogueFunction_1184__type_name( RogueClassFunction_1184* THIS )
{
  return (RogueString*)(Rogue_literal_strings[201]);
}

void RogueFunction_1184__call( RogueClassFunction_1184* THIS )
{
  RogueConsole__reset_input_mode( ROGUE_ARG(THIS->console) );
}

RogueClassFunction_1184* RogueFunction_1184__init__Console( RogueClassFunction_1184* THIS, RogueClassConsole* _auto_1185_0 )
{
  ROGUE_GC_CHECK;
  THIS->console = _auto_1185_0;
  return (RogueClassFunction_1184*)(THIS);
}

RogueClassIOError* RogueIOError__init_object( RogueClassIOError* THIS )
{
  ROGUE_GC_CHECK;
  RogueError__init_object( ROGUE_ARG(((RogueClassError*)THIS)) );
  return (RogueClassIOError*)(THIS);
}

RogueString* RogueIOError__type_name( RogueClassIOError* THIS )
{
  return (RogueString*)(Rogue_literal_strings[205]);
}

RogueClassIOError* RogueIOError___throw( RogueClassIOError* THIS )
{
  ROGUE_GC_CHECK;
  throw THIS;

}

RogueClassFileReader* RogueFileReader__init_object( RogueClassFileReader* THIS )
{
  ROGUE_GC_CHECK;
  RogueObject__init_object( ROGUE_ARG(((RogueObject*)THIS)) );
  THIS->buffer = ((RogueByte_List*)(RogueByte_List__init__Int32( ROGUE_ARG(ROGUE_CREATE_REF(RogueByte_List*,ROGUE_CREATE_OBJECT(Byte_List))), 1024 )));
  return (RogueClassFileReader*)(THIS);
}

RogueString* RogueFileReader__type_name( RogueClassFileReader* THIS )
{
  return (RogueString*)(Rogue_literal_strings[164]);
}

RogueClassFileReader* RogueFileReader__close( RogueClassFileReader* THIS )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(!!(THIS->fp)))
  {
    fclose( THIS->fp );
    THIS->fp = 0;

  }
  THIS->position = 0;
  THIS->count = 0;
  return (RogueClassFileReader*)(THIS);
}

RogueLogical RogueFileReader__has_another( RogueClassFileReader* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueLogical)(((THIS->position) < (THIS->count)));
}

RogueByte RogueFileReader__peek( RogueClassFileReader* THIS )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((THIS->position) == (THIS->count))))
  {
    return (RogueByte)(0);
  }
  if (ROGUE_COND(((THIS->buffer_position) == (THIS->buffer->count))))
  {
    THIS->buffer->count = (RogueInt32) fread( THIS->buffer->data->as_bytes, 1, 1024, THIS->fp );

    THIS->buffer_position = 0;
  }
  return (RogueByte)(THIS->buffer->data->as_bytes[THIS->buffer_position]);
}

RogueByte RogueFileReader__read( RogueClassFileReader* THIS )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((THIS->position) == (THIS->count))))
  {
    return (RogueByte)(0);
  }
  RogueByte result_0 = (((RogueFileReader__peek( ROGUE_ARG(THIS) ))));
  THIS->position = ((THIS->position) + (1));
  ++THIS->buffer_position;
  if (ROGUE_COND(((THIS->position) == (THIS->count))))
  {
    RogueFileReader__close( ROGUE_ARG(THIS) );
  }
  return (RogueByte)(result_0);
}

RogueClassFileReader* RogueFileReader__init__String( RogueClassFileReader* THIS, RogueString* _filepath_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(!(((RogueFileReader__open__String( ROGUE_ARG(THIS), _filepath_0 ))))))
  {
    throw ((RogueClassIOError*)(RogueIOError___throw( ROGUE_ARG(((RogueClassIOError*)(((RogueException*)Rogue_call_ROGUEM4( 13, ROGUE_ARG(((RogueException*)ROGUE_CREATE_REF(RogueClassIOError*,ROGUE_CREATE_OBJECT(IOError)))), ROGUE_ARG(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[24] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], _filepath_0 ))) )))), Rogue_literal_strings[25] )))) )))) ))))) )));
  }
  return (RogueClassFileReader*)(THIS);
}

void RogueFileReader__on_cleanup( RogueClassFileReader* THIS )
{
  ROGUE_GC_CHECK;
  RogueFileReader__close( ROGUE_ARG(THIS) );
}

RogueLogical RogueFileReader__open__String( RogueClassFileReader* THIS, RogueString* _auto_1214_0 )
{
  ROGUE_GC_CHECK;
  THIS->filepath = _auto_1214_0;
  RogueFileReader__close( ROGUE_ARG(THIS) );
  THIS->fp = fopen( (char*)THIS->filepath->utf8, "rb" );
  if ( !THIS->fp ) return false;
  fseek( THIS->fp, 0, SEEK_END );
  THIS->count = (RogueInt32) ftell( THIS->fp );
  fseek( THIS->fp, 0, SEEK_SET );

  if (ROGUE_COND(((THIS->count) == (0))))
  {
    RogueFileReader__close( ROGUE_ARG(THIS) );
  }
  return (RogueLogical)(true);
}

RogueClassUTF8Reader* RogueUTF8Reader__init_object( RogueClassUTF8Reader* THIS )
{
  ROGUE_GC_CHECK;
  RogueObject__init_object( ROGUE_ARG(((RogueObject*)THIS)) );
  return (RogueClassUTF8Reader*)(THIS);
}

RogueString* RogueUTF8Reader__type_name( RogueClassUTF8Reader* THIS )
{
  return (RogueString*)(Rogue_literal_strings[165]);
}

RogueClassUTF8Reader* RogueUTF8Reader__close( RogueClassUTF8Reader* THIS )
{
  ROGUE_GC_CHECK;
  RogueReader_Byte___close( ROGUE_ARG(((((RogueObject*)THIS->byte_reader)))) );
  return (RogueClassUTF8Reader*)(THIS);
}

RogueLogical RogueUTF8Reader__has_another( RogueClassUTF8Reader* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueLogical)(((THIS->next.exists) || ((RogueReader_Byte___has_another( ROGUE_ARG(((((RogueObject*)THIS->byte_reader)))) )))));
}

RogueCharacter RogueUTF8Reader__peek( RogueClassUTF8Reader* THIS )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(THIS->next.exists))
  {
    return (RogueCharacter)(THIS->next.value);
  }
  if (ROGUE_COND(!((RogueReader_Byte___has_another( ROGUE_ARG(((((RogueObject*)THIS->byte_reader)))) )))))
  {
    return (RogueCharacter)((RogueCharacter)0);
  }
  RogueCharacter ch_0 = (((RogueCharacter)((RogueReader_Byte___read( ROGUE_ARG(((((RogueObject*)THIS->byte_reader)))) )))));
  if (ROGUE_COND(!!(((((RogueInt32)(ch_0))) & (128)))))
  {
    if (ROGUE_COND(!!(((((RogueInt32)(ch_0))) & (32)))))
    {
      if (ROGUE_COND(!!(((((RogueInt32)(ch_0))) & (16)))))
      {
        ch_0 = ((RogueCharacter)(((RogueCharacter)(((((((((RogueInt32)(ch_0))) & (7))) << (18))) | (((((((RogueInt32)((RogueReader_Byte___read( ROGUE_ARG(((((RogueObject*)THIS->byte_reader)))) ))))) & (63))) << (12))))))));
        ch_0 |= ((RogueCharacter)(((((((RogueInt32)((RogueReader_Byte___read( ROGUE_ARG(((((RogueObject*)THIS->byte_reader)))) ))))) & (63))) << (6))));
        ch_0 |= ((RogueCharacter)(((((RogueInt32)((RogueReader_Byte___read( ROGUE_ARG(((((RogueObject*)THIS->byte_reader)))) ))))) & (63))));
      }
      else
      {
        ch_0 = ((RogueCharacter)(((RogueCharacter)(((((((((RogueInt32)(ch_0))) & (15))) << (12))) | (((((((RogueInt32)((RogueReader_Byte___read( ROGUE_ARG(((((RogueObject*)THIS->byte_reader)))) ))))) & (63))) << (6))))))));
        ch_0 |= ((RogueCharacter)(((((RogueInt32)((RogueReader_Byte___read( ROGUE_ARG(((((RogueObject*)THIS->byte_reader)))) ))))) & (63))));
      }
    }
    else
    {
      ch_0 = ((RogueCharacter)(((RogueCharacter)(((((((((RogueInt32)(ch_0))) & (31))) << (6))) | (((((RogueInt32)((RogueReader_Byte___read( ROGUE_ARG(((((RogueObject*)THIS->byte_reader)))) ))))) & (63))))))));
    }
  }
  THIS->next = RogueOptionalCharacter( ch_0, true );
  return (RogueCharacter)(ch_0);
}

RogueCharacter RogueUTF8Reader__read( RogueClassUTF8Reader* THIS )
{
  ROGUE_GC_CHECK;
  RogueCharacter result_0 = (((RogueUTF8Reader__peek( ROGUE_ARG(THIS) ))));
  THIS->next = (RogueOptionalCharacter__create());
  THIS->position = ((THIS->position) + (1));
  return (RogueCharacter)(result_0);
}

RogueClassUTF8Reader* RogueUTF8Reader__init__Reader_Byte_( RogueClassUTF8Reader* THIS, RogueClassReader_Byte_* _auto_1238_0 )
{
  ROGUE_GC_CHECK;
  THIS->byte_reader = _auto_1238_0;
  THIS->next = (RogueOptionalCharacter__create());
  return (RogueClassUTF8Reader*)(THIS);
}

RogueClassJSONParseError* RogueJSONParseError__init_object( RogueClassJSONParseError* THIS )
{
  ROGUE_GC_CHECK;
  RogueError__init_object( ROGUE_ARG(((RogueClassError*)THIS)) );
  return (RogueClassJSONParseError*)(THIS);
}

RogueString* RogueJSONParseError__type_name( RogueClassJSONParseError* THIS )
{
  return (RogueString*)(Rogue_literal_strings[206]);
}

RogueClassJSONParseError* RogueJSONParseError__init__String( RogueClassJSONParseError* THIS, RogueString* _auto_1259_0 )
{
  ROGUE_GC_CHECK;
  THIS->message = _auto_1259_0;
  return (RogueClassJSONParseError*)(THIS);
}

RogueClassJSONParseError* RogueJSONParseError___throw( RogueClassJSONParseError* THIS )
{
  ROGUE_GC_CHECK;
  throw THIS;

}

RogueClassJSONParserBuffer* RogueJSONParserBuffer__init_object( RogueClassJSONParserBuffer* THIS )
{
  ROGUE_GC_CHECK;
  RogueStringBuilder__init_object( ROGUE_ARG(((RogueStringBuilder*)THIS)) );
  return (RogueClassJSONParserBuffer*)(THIS);
}

RogueString* RogueJSONParserBuffer__type_name( RogueClassJSONParserBuffer* THIS )
{
  return (RogueString*)(Rogue_literal_strings[193]);
}

RogueString* RogueSystemEnvironment__get__String( RogueClassSystemEnvironment THIS, RogueString* name_0 )
{
  ROGUE_GC_CHECK;
  ROGUE_DEF_LOCAL_REF(RogueString*,result_1,0);
  char* c_result = getenv( (char*)name_0->utf8 );
  if (c_result)
  {
    result_1 = RogueString_create_from_utf8( c_result );
  }

  return (RogueString*)(((((result_1))) ? (ROGUE_ARG(result_1)) : ROGUE_ARG(((RogueString*)(NULL)))));
}


void Rogue_configure( int argc, const char* argv[] )
{
  if (Rogue_configured) return;
  Rogue_configured = 1;

  Rogue_argc = argc;
  Rogue_argv = argv;

  Rogue_thread_register();
  Rogue_configure_gc();
  Rogue_configure_types();
  set_terminate( Rogue_terminate_handler );
  RogueTypeObject = &Rogue_types[ 0 ];
  RogueTypeGlobal = &Rogue_types[ 1 ];
  RogueTypePrintWriter_global_output_buffer_ = &Rogue_types[ 2 ];
  RogueTypePrintWriter = &Rogue_types[ 3 ];
  RogueTypeValueTable = &Rogue_types[ 4 ];
  RogueTypeValue = &Rogue_types[ 5 ];
  RogueTypeTable_String_Value_ = &Rogue_types[ 6 ];
  RogueTypeInt32 = &Rogue_types[ 7 ];
  RogueTypeArray = &Rogue_types[ 9 ];
  RogueTypeTableEntry_String_Value_ = &Rogue_types[ 10 ];
  RogueTypeString = &Rogue_types[ 11 ];
  RogueType_Function_TableEntry_String_Value__TableEntry_String_Value__RETURNSLogical_ = &Rogue_types[ 12 ];
  RogueTypeStringBuilder = &Rogue_types[ 13 ];
  RogueTypeByte_List = &Rogue_types[ 14 ];
  RogueTypeGenericList = &Rogue_types[ 15 ];
  RogueTypeByte = &Rogue_types[ 16 ];
  RogueTypeStringBuilderPool = &Rogue_types[ 18 ];
  RogueTypeStringBuilder_List = &Rogue_types[ 19 ];
  RogueTypeLogical = &Rogue_types[ 21 ];
  RogueType_Function____List = &Rogue_types[ 22 ];
  RogueType_Function___ = &Rogue_types[ 23 ];
  RogueTypeException = &Rogue_types[ 25 ];
  RogueTypeStackTrace = &Rogue_types[ 26 ];
  RogueTypeString_List = &Rogue_types[ 27 ];
  RogueTypeReal64 = &Rogue_types[ 29 ];
  RogueTypeInt64 = &Rogue_types[ 30 ];
  RogueTypeCharacter = &Rogue_types[ 31 ];
  RogueTypeCharacter_List = &Rogue_types[ 32 ];
  RogueTypeValueList = &Rogue_types[ 34 ];
  RogueTypeValue_List = &Rogue_types[ 35 ];
  RogueTypeStringConsolidationTable = &Rogue_types[ 37 ];
  RogueTypeStringTable_String_ = &Rogue_types[ 38 ];
  RogueTypeTable_String_String_ = &Rogue_types[ 39 ];
  RogueTypeTableEntry_String_String_ = &Rogue_types[ 41 ];
  RogueType_Function_TableEntry_String_String__TableEntry_String_String__RETURNSLogical_ = &Rogue_types[ 42 ];
  RogueTypeReader_Character_ = &Rogue_types[ 43 ];
  RogueTypeReader_String_ = &Rogue_types[ 44 ];
  RogueTypeConsole = &Rogue_types[ 45 ];
  RogueTypePrintWriter_output_buffer_ = &Rogue_types[ 46 ];
  RogueTypeConsoleErrorPrinter = &Rogue_types[ 47 ];
  RogueTypePrimitiveWorkBuffer = &Rogue_types[ 48 ];
  RogueTypeMath = &Rogue_types[ 49 ];
  RogueTypeFunction_221 = &Rogue_types[ 50 ];
  RogueTypeSystem = &Rogue_types[ 51 ];
  RogueTypeError = &Rogue_types[ 52 ];
  RogueTypeFile = &Rogue_types[ 53 ];
  RogueTypeLineReader = &Rogue_types[ 54 ];
  RogueTypeReader_Byte_ = &Rogue_types[ 55 ];
  RogueTypeFileWriter = &Rogue_types[ 56 ];
  RogueTypeWriter_Byte_ = &Rogue_types[ 57 ];
  RogueTypeScanner = &Rogue_types[ 58 ];
  RogueTypeJSONParser = &Rogue_types[ 59 ];
  RogueTypeJSON = &Rogue_types[ 60 ];
  RogueType_Function_String_RETURNSString_ = &Rogue_types[ 61 ];
  RogueTypeFunction_283 = &Rogue_types[ 62 ];
  RogueTypeRuntime = &Rogue_types[ 63 ];
  RogueTypeWeakReference = &Rogue_types[ 64 ];
  RogueTypePrintWriterAdapter = &Rogue_types[ 65 ];
  RogueTypePrintWriter_buffer_ = &Rogue_types[ 66 ];
  RogueTypeLogicalValue = &Rogue_types[ 67 ];
  RogueTypeReal64Value = &Rogue_types[ 68 ];
  RogueTypeNullValue = &Rogue_types[ 69 ];
  RogueTypeStringValue = &Rogue_types[ 70 ];
  RogueTypeUndefinedValue = &Rogue_types[ 71 ];
  RogueTypeOutOfBoundsError = &Rogue_types[ 72 ];
  RogueTypeListRewriter_Character_ = &Rogue_types[ 73 ];
  RogueTypeFunction_1184 = &Rogue_types[ 74 ];
  RogueTypeIOError = &Rogue_types[ 75 ];
  RogueTypeFileReader = &Rogue_types[ 76 ];
  RogueTypeUTF8Reader = &Rogue_types[ 77 ];
  RogueTypeJSONParseError = &Rogue_types[ 78 ];
  RogueTypeJSONParserBuffer = &Rogue_types[ 79 ];
  RogueTypeOptionalInt32 = &Rogue_types[ 80 ];
  RogueTypeSystemEnvironment = &Rogue_types[ 81 ];
  RogueTypeOptionalByte = &Rogue_types[ 82 ];
  RogueTypeOptionalCharacter = &Rogue_types[ 83 ];

  Rogue_literal_strings[0] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "", 0 ) );
  Rogue_literal_strings[1] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "null", 4 ) );
  Rogue_literal_strings[2] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( ".", 1 ) );
  Rogue_literal_strings[3] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "/", 1 ) );
  Rogue_literal_strings[4] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Exception", 9 ) );
  Rogue_literal_strings[5] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "=", 1 ) );
  Rogue_literal_strings[6] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( ",", 1 ) );
  Rogue_literal_strings[7] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "\n", 1 ) );
  Rogue_literal_strings[8] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "(", 1 ) );
  Rogue_literal_strings[9] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Object", 6 ) );
  Rogue_literal_strings[10] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( " 0x", 3 ) );
  Rogue_literal_strings[11] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( ")", 1 ) );
  Rogue_literal_strings[12] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "No stack trace", 14 ) );
  Rogue_literal_strings[13] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Unknown", 7 ) );
  Rogue_literal_strings[14] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Could not get absolute path", 27 ) );
  Rogue_literal_strings[15] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( ".rogo", 5 ) );
  Rogue_literal_strings[16] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "roguec", 6 ) );
  Rogue_literal_strings[17] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "filepath", 8 ) );
  Rogue_literal_strings[18] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( ".rogo/Cache.json", 16 ) );
  Rogue_literal_strings[19] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Identifier expected.", 20 ) );
  Rogue_literal_strings[20] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "'", 1 ) );
  Rogue_literal_strings[21] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "' expected.", 11 ) );
  Rogue_literal_strings[22] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "true", 4 ) );
  Rogue_literal_strings[23] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "false", 5 ) );
  Rogue_literal_strings[24] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Unable to open ", 15 ) );
  Rogue_literal_strings[25] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( " for reading.", 13 ) );
  Rogue_literal_strings[26] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "cpp", 3 ) );
  Rogue_literal_strings[27] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "BuildCore.rogue", 15 ) );
  Rogue_literal_strings[28] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "--build", 7 ) );
  Rogue_literal_strings[29] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "--build=", 8 ) );
  Rogue_literal_strings[30] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "ERROR: expected filename after \"--build=\".", 42 ) );
  Rogue_literal_strings[31] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( ".rogue", 6 ) );
  Rogue_literal_strings[32] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "ERROR: no such file \"", 21 ) );
  Rogue_literal_strings[33] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "\".", 2 ) );
  Rogue_literal_strings[34] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Index ", 6 ) );
  Rogue_literal_strings[35] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "-9223372036854775808", 20 ) );
  Rogue_literal_strings[36] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( " is out of bounds (data has zero elements).", 43 ) );
  Rogue_literal_strings[37] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( " is out of bounds (data has one element at index 0).", 52 ) );
  Rogue_literal_strings[38] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( " is out of bounds (data has ", 28 ) );
  Rogue_literal_strings[39] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "# element", 9 ) );
  Rogue_literal_strings[40] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "#", 1 ) );
  Rogue_literal_strings[41] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "es", 2 ) );
  Rogue_literal_strings[42] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "s", 1 ) );
  Rogue_literal_strings[43] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( " indices ", 9 ) );
  Rogue_literal_strings[44] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "..", 2 ) );
  Rogue_literal_strings[45] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( ").", 2 ) );
  Rogue_literal_strings[46] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Build.rogue", 11 ) );
  Rogue_literal_strings[47] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Creating skeleton Build.rogue...", 32 ) );
  Rogue_literal_strings[48] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( " for writing.", 13 ) );
  Rogue_literal_strings[49] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "module Build\n", 13 ) );
  Rogue_literal_strings[50] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "ERROR: Build.rogue does not exist and no alternate specified with --build=<filename>.", 85 ) );
  Rogue_literal_strings[51] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( ".rogo/Build", 11 ) );
  Rogue_literal_strings[52] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( ".rogo/", 6 ) );
  Rogue_literal_strings[53] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Recompiling ", 12 ) );
  Rogue_literal_strings[54] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "...", 3 ) );
  Rogue_literal_strings[55] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "#$", 2 ) );
  Rogue_literal_strings[56] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "config", 6 ) );
  Rogue_literal_strings[57] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "libraries", 9 ) );
  Rogue_literal_strings[58] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( " \n()", 4 ) );
  Rogue_literal_strings[59] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "name", 4 ) );
  Rogue_literal_strings[60] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( " ", 1 ) );
  Rogue_literal_strings[61] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "$HEADER(", 8 ) );
  Rogue_literal_strings[62] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "installed_libraries", 19 ) );
  Rogue_literal_strings[63] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Linux", 5 ) );
  Rogue_literal_strings[64] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "iOS", 3 ) );
  Rogue_literal_strings[65] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "macOS", 5 ) );
  Rogue_literal_strings[66] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Windows", 7 ) );
  Rogue_literal_strings[67] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Android", 7 ) );
  Rogue_literal_strings[68] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Cygwin", 6 ) );
  Rogue_literal_strings[69] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "emscripten", 10 ) );
  Rogue_literal_strings[70] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Process was not created", 23 ) );
  Rogue_literal_strings[71] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "brew list ", 10 ) );
  Rogue_literal_strings[72] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( " > /dev/null 2>&1", 17 ) );
  Rogue_literal_strings[73] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "IDE", 3 ) );
  Rogue_literal_strings[74] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "TARGET", 6 ) );
  Rogue_literal_strings[75] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "/Users/abe/Projects/Rogue/Source/Tools/Rogo/Rogo.rogue", 54 ) );
  Rogue_literal_strings[76] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( ":", 1 ) );
  Rogue_literal_strings[77] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( ": error:Run 'make ", 18 ) );
  Rogue_literal_strings[78] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "' from the command line to install necessary libraries.", 55 ) );
  Rogue_literal_strings[79] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "\nLibrary '", 10 ) );
  Rogue_literal_strings[80] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "' must be installed.  Install now (y/n)? ", 41 ) );
  Rogue_literal_strings[81] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Missing required library '", 26 ) );
  Rogue_literal_strings[82] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "'.", 2 ) );
  Rogue_literal_strings[83] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "install", 7 ) );
  Rogue_literal_strings[84] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "brew install ", 13 ) );
  Rogue_literal_strings[85] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Failed to install library '", 27 ) );
  Rogue_literal_strings[86] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Finding ", 8 ) );
  Rogue_literal_strings[87] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( " library...", 11 ) );
  Rogue_literal_strings[88] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( " > .rogo/Build.temp", 19 ) );
  Rogue_literal_strings[89] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Cannot locate library '", 23 ) );
  Rogue_literal_strings[90] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "header", 6 ) );
  Rogue_literal_strings[91] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "**/", 3 ) );
  Rogue_literal_strings[92] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( ".rogo/Build.temp", 16 ) );
  Rogue_literal_strings[93] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "library", 7 ) );
  Rogue_literal_strings[94] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "*.h", 3 ) );
  Rogue_literal_strings[95] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "  Header path:  ", 16 ) );
  Rogue_literal_strings[96] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "*.a", 3 ) );
  Rogue_literal_strings[97] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "*.dylib", 7 ) );
  Rogue_literal_strings[98] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "*.so", 4 ) );
  Rogue_literal_strings[99] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "  Library path: ", 16 ) );
  Rogue_literal_strings[100] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "header_locations", 16 ) );
  Rogue_literal_strings[101] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "library_locations", 17 ) );
  Rogue_literal_strings[102] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Library not installed: '", 24 ) );
  Rogue_literal_strings[103] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "dpkg -L ", 8 ) );
  Rogue_literal_strings[104] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Checking for library ", 21 ) );
  Rogue_literal_strings[105] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "sudo apt-get install ", 21 ) );
  Rogue_literal_strings[106] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Rogo does not know how to install a library for \"", 49 ) );
  Rogue_literal_strings[107] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "$LIBRARY(", 9 ) );
  Rogue_literal_strings[108] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "roguec_args", 11 ) );
  Rogue_literal_strings[109] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( " --target=C++,Console,", 22 ) );
  Rogue_literal_strings[110] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( " .rogo/", 7 ) );
  Rogue_literal_strings[111] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( " .rogo/BuildCore.rogue ", 23 ) );
  Rogue_literal_strings[112] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( " --debug --main --output=", 25 ) );
  Rogue_literal_strings[113] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "ERROR compiling ", 16 ) );
  Rogue_literal_strings[114] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "cpp_args", 8 ) );
  Rogue_literal_strings[115] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( ".cpp", 4 ) );
  Rogue_literal_strings[116] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "brew_installed", 14 ) );
  Rogue_literal_strings[117] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "which brew > /dev/null 2>&1", 27 ) );
  Rogue_literal_strings[118] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "\nHomebrew must be installed.  Install now (y/n)? ", 49 ) );
  Rogue_literal_strings[119] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "/usr/bin/ruby -e \"$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install)\"", 98 ) );
  Rogue_literal_strings[120] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Failed to install Homebrew.", 27 ) );
  Rogue_literal_strings[121] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Missing required dependency 'brew' (Homebrew).", 46 ) );
  Rogue_literal_strings[122] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( " -I \"", 5 ) );
  Rogue_literal_strings[123] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "\"", 1 ) );
  Rogue_literal_strings[124] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( " -L \"", 5 ) );
  Rogue_literal_strings[125] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( " -o ", 4 ) );
  Rogue_literal_strings[126] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "link", 4 ) );
  Rogue_literal_strings[127] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "ROGUE_GC_THRESHOLD", 18 ) );
  Rogue_literal_strings[128] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "MB", 2 ) );
  Rogue_literal_strings[129] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "KB", 2 ) );
  Rogue_literal_strings[130] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "0.0", 3 ) );
  Rogue_literal_strings[131] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "-infinity", 9 ) );
  Rogue_literal_strings[132] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "infinity", 8 ) );
  Rogue_literal_strings[133] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "NaN", 3 ) );
  Rogue_literal_strings[134] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Global", 6 ) );
  Rogue_literal_strings[135] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Value", 5 ) );
  Rogue_literal_strings[136] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "String", 6 ) );
  Rogue_literal_strings[137] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "StringBuilder", 13 ) );
  Rogue_literal_strings[138] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "GenericList", 11 ) );
  Rogue_literal_strings[139] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Array", 5 ) );
  Rogue_literal_strings[140] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Table<<String,String>>", 22 ) );
  Rogue_literal_strings[141] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "StackTrace", 10 ) );
  Rogue_literal_strings[142] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Console", 7 ) );
  Rogue_literal_strings[143] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Math", 4 ) );
  Rogue_literal_strings[144] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "(Function())", 12 ) );
  Rogue_literal_strings[145] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "System", 6 ) );
  Rogue_literal_strings[146] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "File", 4 ) );
  Rogue_literal_strings[147] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "LineReader", 10 ) );
  Rogue_literal_strings[148] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "FileWriter", 10 ) );
  Rogue_literal_strings[149] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Scanner", 7 ) );
  Rogue_literal_strings[150] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "JSONParser", 10 ) );
  Rogue_literal_strings[151] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Table<<String,Value>>", 21 ) );
  Rogue_literal_strings[152] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "JSON", 4 ) );
  Rogue_literal_strings[153] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "ConsoleErrorPrinter", 19 ) );
  Rogue_literal_strings[154] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "(Function(String)->String)", 26 ) );
  Rogue_literal_strings[155] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Runtime", 7 ) );
  Rogue_literal_strings[156] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "StringBuilderPool", 17 ) );
  Rogue_literal_strings[157] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "WeakReference", 13 ) );
  Rogue_literal_strings[158] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "TableEntry<<String,Value>>", 26 ) );
  Rogue_literal_strings[159] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "(Function(TableEntry<<String,Value>>,TableEntry<<String,Value>>)->Logical)", 74 ) );
  Rogue_literal_strings[160] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "TableEntry<<String,String>>", 27 ) );
  Rogue_literal_strings[161] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "(Function(TableEntry<<String,String>>,TableEntry<<String,String>>)->Logical)", 76 ) );
  Rogue_literal_strings[162] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "PrintWriterAdapter", 18 ) );
  Rogue_literal_strings[163] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "ListRewriter<<Character>>", 25 ) );
  Rogue_literal_strings[164] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "FileReader", 10 ) );
  Rogue_literal_strings[165] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "UTF8Reader", 10 ) );
  Rogue_literal_strings[166] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "ValueList", 9 ) );
  Rogue_literal_strings[167] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "ValueTable", 10 ) );
  Rogue_literal_strings[168] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "LogicalValue", 12 ) );
  Rogue_literal_strings[169] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Real64Value", 11 ) );
  Rogue_literal_strings[170] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "NullValue", 9 ) );
  Rogue_literal_strings[171] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "StringValue", 11 ) );
  Rogue_literal_strings[172] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "TRUE", 4 ) );
  Rogue_literal_strings[173] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "yes", 3 ) );
  Rogue_literal_strings[174] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "YES", 3 ) );
  Rogue_literal_strings[175] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "1", 1 ) );
  Rogue_literal_strings[176] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "\\\"", 2 ) );
  Rogue_literal_strings[177] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "\\\\", 2 ) );
  Rogue_literal_strings[178] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "\\b", 2 ) );
  Rogue_literal_strings[179] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "\\f", 2 ) );
  Rogue_literal_strings[180] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "\\n", 2 ) );
  Rogue_literal_strings[181] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "\\r", 2 ) );
  Rogue_literal_strings[182] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "\\t", 2 ) );
  Rogue_literal_strings[183] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "\\u", 2 ) );
  Rogue_literal_strings[184] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Array<<String>>", 15 ) );
  Rogue_literal_strings[185] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Array<<(Function())>>", 21 ) );
  Rogue_literal_strings[186] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Array<<Byte>>", 13 ) );
  Rogue_literal_strings[187] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Array<<TableEntry<<String,Value>>>>", 35 ) );
  Rogue_literal_strings[188] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Array<<StringBuilder>>", 22 ) );
  Rogue_literal_strings[189] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Array<<Character>>", 18 ) );
  Rogue_literal_strings[190] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Array<<Value>>", 14 ) );
  Rogue_literal_strings[191] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Array<<TableEntry<<String,String>>>>", 36 ) );
  Rogue_literal_strings[192] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "PrimitiveWorkBuffer", 19 ) );
  Rogue_literal_strings[193] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "JSONParserBuffer", 16 ) );
  Rogue_literal_strings[194] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Byte[]", 6 ) );
  Rogue_literal_strings[195] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Character[]", 11 ) );
  Rogue_literal_strings[196] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Value[]", 7 ) );
  Rogue_literal_strings[197] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "String[]", 8 ) );
  Rogue_literal_strings[198] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "(Function())[]", 14 ) );
  Rogue_literal_strings[199] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "StringBuilder[]", 15 ) );
  Rogue_literal_strings[200] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Function_221", 12 ) );
  Rogue_literal_strings[201] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Function_1184", 13 ) );
  Rogue_literal_strings[202] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Error", 5 ) );
  Rogue_literal_strings[203] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "StringTable<<String>>", 21 ) );
  Rogue_literal_strings[204] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "OutOfBoundsError", 16 ) );
  Rogue_literal_strings[205] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "IOError", 7 ) );
  Rogue_literal_strings[206] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "JSONParseError", 14 ) );
  Rogue_literal_strings[207] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Function_283", 12 ) );
  Rogue_literal_strings[208] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "UndefinedValue", 14 ) );
  Rogue_literal_strings[209] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "StringConsolidationTable", 24 ) );
  Rogue_literal_strings[210] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "PrintWriter<<global_output_buffer>>", 35 ) );
  Rogue_literal_strings[211] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "PrintWriter", 11 ) );
  Rogue_literal_strings[212] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Int32", 5 ) );
  Rogue_literal_strings[213] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Byte", 4 ) );
  Rogue_literal_strings[214] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Logical", 7 ) );
  Rogue_literal_strings[215] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Real64", 6 ) );
  Rogue_literal_strings[216] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Int64", 5 ) );
  Rogue_literal_strings[217] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Character", 9 ) );
  Rogue_literal_strings[218] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Reader<<Character>>", 19 ) );
  Rogue_literal_strings[219] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Reader<<String>>", 16 ) );
  Rogue_literal_strings[220] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "PrintWriter<<output_buffer>>", 28 ) );
  Rogue_literal_strings[221] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Reader<<Byte>>", 14 ) );
  Rogue_literal_strings[222] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Writer<<Byte>>", 14 ) );
  Rogue_literal_strings[223] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "PrintWriter<<buffer>>", 21 ) );
  Rogue_literal_strings[224] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Int32?", 6 ) );
  Rogue_literal_strings[225] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "SystemEnvironment", 17 ) );
  Rogue_literal_strings[226] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Byte?", 5 ) );
  Rogue_literal_strings[227] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Character?", 10 ) );
  Rogue_literal_strings[228] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "cache", 5 ) );
  Rogue_literal_strings[229] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "console", 7 ) );
  Rogue_literal_strings[230] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "global_output_buffer", 20 ) );
  Rogue_literal_strings[231] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "exit_functions", 14 ) );
  Rogue_literal_strings[232] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "data", 4 ) );
  Rogue_literal_strings[233] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "count", 5 ) );
  Rogue_literal_strings[234] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "bin_mask", 8 ) );
  Rogue_literal_strings[235] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "cur_entry_index", 15 ) );
  Rogue_literal_strings[236] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "bins", 4 ) );
  Rogue_literal_strings[237] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "first_entry", 11 ) );
  Rogue_literal_strings[238] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "last_entry", 10 ) );
  Rogue_literal_strings[239] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "cur_entry", 9 ) );
  Rogue_literal_strings[240] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "sort_function", 13 ) );
  Rogue_literal_strings[241] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "key", 3 ) );
  Rogue_literal_strings[242] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "value", 5 ) );
  Rogue_literal_strings[243] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "adjacent_entry", 14 ) );
  Rogue_literal_strings[244] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "next_entry", 10 ) );
  Rogue_literal_strings[245] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "previous_entry", 14 ) );
  Rogue_literal_strings[246] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "hash", 4 ) );
  Rogue_literal_strings[247] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "work_bytes", 10 ) );
  Rogue_literal_strings[248] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "pool", 4 ) );
  Rogue_literal_strings[249] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "utf8", 4 ) );
  Rogue_literal_strings[250] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "indent", 6 ) );
  Rogue_literal_strings[251] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "cursor_offset", 13 ) );
  Rogue_literal_strings[252] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "cursor_index", 12 ) );
  Rogue_literal_strings[253] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "at_newline", 10 ) );
  Rogue_literal_strings[254] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "available", 9 ) );
  Rogue_literal_strings[255] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "message", 7 ) );
  Rogue_literal_strings[256] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "stack_trace", 11 ) );
  Rogue_literal_strings[257] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "entries", 7 ) );
  Rogue_literal_strings[258] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "is_formatted", 12 ) );
  Rogue_literal_strings[259] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "position", 8 ) );
  Rogue_literal_strings[260] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "error", 5 ) );
  Rogue_literal_strings[261] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "immediate_mode", 14 ) );
  Rogue_literal_strings[262] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "is_blocking", 11 ) );
  Rogue_literal_strings[263] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "decode_bytes", 12 ) );
  Rogue_literal_strings[264] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "output_buffer", 13 ) );
  Rogue_literal_strings[265] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "input_buffer", 12 ) );
  Rogue_literal_strings[266] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "next_input_character", 20 ) );
  Rogue_literal_strings[267] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "input_bytes", 11 ) );
  Rogue_literal_strings[268] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "termios original_terminal_settings;", 35 ) );
  Rogue_literal_strings[269] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "int     original_stdin_flags;", 29 ) );
  Rogue_literal_strings[270] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "command_line_arguments", 22 ) );
  Rogue_literal_strings[271] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "executable_filepath", 19 ) );
  Rogue_literal_strings[272] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "environment", 11 ) );
  Rogue_literal_strings[273] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "source", 6 ) );
  Rogue_literal_strings[274] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "next", 4 ) );
  Rogue_literal_strings[275] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "buffer", 6 ) );
  Rogue_literal_strings[276] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "prev", 4 ) );
  Rogue_literal_strings[277] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "FILE* fp;", 9 ) );
  Rogue_literal_strings[278] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "line", 4 ) );
  Rogue_literal_strings[279] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "column", 6 ) );
  Rogue_literal_strings[280] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "spaces_per_tab", 14 ) );
  Rogue_literal_strings[281] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "reader", 6 ) );
  Rogue_literal_strings[282] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "next_weak_reference", 19 ) );
  Rogue_literal_strings[283] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "RogueObject* value;", 19 ) );
  Rogue_literal_strings[284] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "output", 6 ) );
  Rogue_literal_strings[285] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "true_value", 10 ) );
  Rogue_literal_strings[286] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "false_value", 11 ) );
  Rogue_literal_strings[287] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "empty_string", 12 ) );
  Rogue_literal_strings[288] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "list", 4 ) );
  Rogue_literal_strings[289] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "read_index", 10 ) );
  Rogue_literal_strings[290] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "write_index", 11 ) );
  Rogue_literal_strings[291] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "buffer_position", 15 ) );
  Rogue_literal_strings[292] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "byte_reader", 11 ) );
  Rogue_literal_strings[293] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "exists", 6 ) );

}

void Rogue_init_thread()
{
}

void Rogue_deinit_thread()
{
}

#ifdef ROGUE_AUTO_LAUNCH
__attribute__((constructor))
#endif
void Rogue_launch()
{
  Rogue_configure(0, NULL);
  RogueStringBuilder__init_class();
  RogueSystem__init_class();
  RogueRuntime__init_class();
  RogueLogicalValue__init_class();
  RogueStringValue__init_class();

  Rogue_init_thread();

  RogueSystem_executable_filepath = RogueString_create_from_utf8(
      Rogue_argc ? Rogue_argv[0] : "Rogue", -1 );

  for (int i=1; i<Rogue_argc; ++i)
  {
    RogueString_List__add__String( RogueSystem_command_line_arguments,
        RogueString_create_from_utf8( Rogue_argv[i], -1 ) );
  }

  // Instantiate essential singletons
  ROGUE_SINGLETON( Global );

  RogueGlobal__on_launch( (RogueClassGlobal*) (ROGUE_SINGLETON(Global)) );
  Rogue_collect_garbage();
}

bool Rogue_update_tasks()
{
  // Returns true if any tasks are still active
  try
  {
    Rogue_collect_garbage();
    return false;
  }
  catch (RogueException* err)
  {
    printf( "Uncaught exception\n" );
    RogueException__display( err );
    return false;
  }
}

int main( int argc, const char* argv[] )
{
  try
  {
    Rogue_configure( argc, argv );
    Rogue_launch();

    while (Rogue_update_tasks()) {}

    Rogue_quit();
  }
  catch (RogueException* err)
  {
    printf( "Uncaught exception\n" );
    RogueException__display( err );
  }

  return 0;
}
