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
      RogueTypeInfo* RogueType_type_info( RogueType* THIS )
      {
        if ( !THIS->type_info )
        {
          THIS->type_info = RogueTypeInfo__init__Int32_String( (RogueTypeInfo*)ROGUE_CREATE_OBJECT(TypeInfo),
              THIS->index, Rogue_literal_strings[ THIS->name_index ] );

          for (int i=0; i<THIS->global_property_count; ++i)
          {
            RogueTypeInfo__add_global_property_info__Int32_Int32( (RogueTypeInfo*) THIS->type_info,
                THIS->global_property_name_indices[i], THIS->global_property_type_indices[i] );
          }

          for (int i=0; i<THIS->property_count; ++i)
          {
            RogueTypeInfo__add_property_info__Int32_Int32( (RogueTypeInfo*) THIS->type_info,
                THIS->property_name_indices[i], THIS->property_type_indices[i] );
          }

          for (int i=0; i<THIS->method_count; ++i)
          {
            RogueTypeInfo__add_method_info__Int32( (RogueTypeInfo*) THIS->type_info,
              (RogueInt32)(THIS->methods - Rogue_dynamic_method_table + i) );
          }
        }

        return (RogueTypeInfo*) THIS->type_info;
      }
typedef void(*ROGUEM0)(void*);
typedef void*(*ROGUEM1)(void*);
typedef RogueInt64(*ROGUEM2)(void*);
typedef void*(*ROGUEM3)(void*,void*);
typedef void(*ROGUEM4)(void*,void*);
typedef void*(*ROGUEM5)(void*,void*,void*);
typedef RogueLogical(*ROGUEM6)(void*,void*);
typedef RogueInt32(*ROGUEM7)(void*);
typedef void*(*ROGUEM8)(void*,RogueInt32);
typedef RogueLogical(*ROGUEM9)(void*);
typedef RogueLogical(*ROGUEM10)(void*,void*,RogueLogical,RogueLogical);
typedef RogueByte(*ROGUEM11)(void*);
typedef RogueCharacter(*ROGUEM12)(void*);
typedef RogueReal64(*ROGUEM13)(void*);
typedef RogueReal32(*ROGUEM14)(void*);
typedef void*(*ROGUEM15)(void*,RogueLogical,RogueLogical);
typedef void*(*ROGUEM16)(void*,void*,RogueLogical,RogueLogical);
typedef void*(*ROGUEM17)(void*,void*,RogueInt32);
typedef void*(*ROGUEM18)(RogueCharacter);
typedef void*(*ROGUEM19)(RogueLogical);
typedef void*(*ROGUEM20)(RogueByte);
typedef void*(*ROGUEM21)(RogueInt32);
typedef void*(*ROGUEM22)(RogueInt64);
typedef void*(*ROGUEM23)(RogueReal32);
typedef void*(*ROGUEM24)(RogueReal64);
typedef RogueInt32(*ROGUEM25)(RogueInt32);
typedef RogueInt32(*ROGUEM26)(RogueInt32,RogueInt32);
typedef RogueCharacter(*ROGUEM27)(RogueInt32);
typedef RogueInt32(*ROGUEM28)(RogueReal64);
typedef void(*ROGUEM29)(void*,RogueInt32,RogueInt32);
typedef void*(*ROGUEM30)(void*,void*,void*,RogueInt32);
typedef void*(*ROGUEM31)(void*,RogueCharacter);
typedef RogueLogical(*ROGUEM32)(void*,RogueCharacter);
typedef RogueLogical(*ROGUEM33)(void*,void*,RogueInt32);
typedef void*(*ROGUEM34)(void*,RogueInt32,RogueInt32);
typedef void*(*ROGUEM35)(void*,RogueInt32,RogueCharacter);
typedef RogueOptionalInt32(*ROGUEM36)(void*,RogueCharacter,RogueOptionalInt32);
typedef RogueOptionalInt32(*ROGUEM37)(void*,void*,RogueOptionalInt32);
typedef void*(*ROGUEM38)(void*,RogueLogical);
typedef void*(*ROGUEM39)(void*,RogueReal64);
typedef void*(*ROGUEM40)(void*,RogueInt32,void*);
typedef void*(*ROGUEM41)(void*,RogueInt32,void*,void*);
typedef RogueLogical(*ROGUEM42)(void*,void*,void*);
typedef RogueCharacter(*ROGUEM43)(void*,RogueInt32);
typedef RogueOptionalInt32(*ROGUEM44)(void*,RogueCharacter);
typedef void*(*ROGUEM45)(void*,RogueCharacter,RogueLogical);
typedef void*(*ROGUEM46)(void*,RogueInt64);
typedef void*(*ROGUEM47)(void*,RogueReal64,RogueInt32);
typedef void(*ROGUEM48)();
typedef void*(*ROGUEM49)(void*,RogueByte);
typedef void*(*ROGUEM50)(void*,RogueByte,RogueInt32);
typedef RogueByte(*ROGUEM51)(void*,RogueInt32);
typedef void*(*ROGUEM52)(void*,RogueInt32,RogueOptionalInt32,RogueInt32,RogueOptionalByte);
typedef RogueByte(*ROGUEM53)(RogueReal64);
typedef RogueLogical(*ROGUEM54)(RogueReal64);
typedef RogueLogical(*ROGUEM55)(RogueInt32);
typedef RogueOptionalInt32(*ROGUEM56)(void*,void*);
typedef void*(*ROGUEM57)(RogueReal64,RogueOptionalInt32);
typedef RogueReal64(*ROGUEM58)(RogueReal64);
typedef RogueReal32(*ROGUEM59)(RogueReal64);
typedef void*(*ROGUEM60)(RogueInt64,RogueInt32,RogueInt32,void*);
typedef void*(*ROGUEM61)(RogueInt64,RogueInt32);
typedef RogueInt64(*ROGUEM62)(RogueReal64);
typedef RogueLogical(*ROGUEM63)(RogueCharacter,RogueLogical,RogueLogical);
typedef RogueLogical(*ROGUEM64)(RogueCharacter);
typedef RogueInt32(*ROGUEM65)(RogueCharacter,RogueInt32);
typedef RogueCharacter(*ROGUEM66)(RogueReal64);
typedef void(*ROGUEM67)(void*,RogueInt32);
typedef RogueInt32(*ROGUEM68)();
typedef void*(*ROGUEM69)(void*,RogueInt32,RogueInt32,RogueInt32);
typedef void*(*ROGUEM70)(void*,RogueInt32,RogueInt32,RogueInt32,RogueInt32,RogueInt32,RogueInt32);
typedef void*(*ROGUEM71)(RogueInt64,RogueInt32,void*);
typedef void*(*ROGUEM72)(void*,RogueInt32,void*,RogueInt32);
typedef RogueInt64(*ROGUEM73)(RogueInt64,RogueInt64);
typedef RogueReal64(*ROGUEM74)(RogueReal64,RogueReal64);
typedef void(*ROGUEM75)(RogueInt32);
typedef void*(*ROGUEM76)();
typedef RogueLogical(*ROGUEM77)(void*,RogueInt32,RogueInt32,void*,RogueInt32,RogueInt32);
typedef void*(*ROGUEM78)(void*,void*,RogueLogical);
typedef RogueLogical(*ROGUEM79)(void*,void*,RogueLogical);
typedef void*(*ROGUEM80)(void*,void*,RogueInt32,RogueLogical);
typedef void*(*ROGUEM81)(void*,RogueCharacter,RogueCharacter);
typedef void*(*ROGUEM82)(void*,RogueOptionalInt32);
typedef RogueOptionalInt32(*ROGUEM83)(void*);
typedef void*(*ROGUEM84)(void*,RogueClassSystemEnvironment);
typedef RogueClassSystemEnvironment(*ROGUEM85)(void*);
typedef void*(*ROGUEM86)(void*,RogueOptionalByte);
typedef RogueOptionalByte(*ROGUEM87)(void*);
typedef void*(*ROGUEM88)(void*,RogueOptionalValue);
typedef RogueOptionalValue(*ROGUEM89)(void*);
typedef void*(*ROGUEM90)(void*,RogueOptionalStringBuilder);
typedef RogueOptionalStringBuilder(*ROGUEM91)(void*);
typedef void*(*ROGUEM92)(void*,RogueOptional_Function___);
typedef RogueOptional_Function___(*ROGUEM93)(void*);
typedef void*(*ROGUEM94)(void*,RogueOptionalString);
typedef RogueOptionalString(*ROGUEM95)(void*);
typedef void*(*ROGUEM96)(void*,RogueOptionalPropertyInfo);
typedef RogueOptionalPropertyInfo(*ROGUEM97)(void*);
typedef void*(*ROGUEM98)(void*,RogueOptionalMethodInfo);
typedef RogueOptionalMethodInfo(*ROGUEM99)(void*);
typedef void*(*ROGUEM100)(void*,RogueOptionalCharacter);
typedef RogueOptionalCharacter(*ROGUEM101)(void*);
typedef void*(*ROGUEM102)(void*,RogueOptionalReal64);
typedef RogueOptionalReal64(*ROGUEM103)(void*);
typedef void*(*ROGUEM104)(void*,RogueOptionalReal32);
typedef RogueOptionalReal32(*ROGUEM105)(void*);
typedef void*(*ROGUEM106)(void*,RogueOptionalInt64);
typedef RogueOptionalInt64(*ROGUEM107)(void*);
typedef void*(*ROGUEM108)(void*,RogueClassFileOptions);
typedef RogueClassFileOptions(*ROGUEM109)(void*);
typedef void*(*ROGUEM110)(void*,RogueOptionalTableEntry_String_Value_);
typedef RogueOptionalTableEntry_String_Value_(*ROGUEM111)(void*);
typedef void*(*ROGUEM112)(void*,RogueOptionalTypeInfo);
typedef RogueOptionalTypeInfo(*ROGUEM113)(void*);
typedef void*(*ROGUEM114)(void*,RogueOptionalTableEntry_Int32_String_);
typedef RogueOptionalTableEntry_Int32_String_(*ROGUEM115)(void*);
typedef void*(*ROGUEM116)(void*,RogueOptionalTableEntry_String_String_);
typedef RogueOptionalTableEntry_String_String_(*ROGUEM117)(void*);
typedef void*(*ROGUEM118)(void*,RogueOptionalTableEntry_String_TypeInfo_);
typedef RogueOptionalTableEntry_String_TypeInfo_(*ROGUEM119)(void*);
typedef void*(*ROGUEM120)(RogueOptionalInt32);
typedef RogueOptionalInt32(*ROGUEM121)();
typedef void*(*ROGUEM122)(RogueClassSystemEnvironment);
typedef void*(*ROGUEM123)(RogueClassSystemEnvironment,void*);
typedef void*(*ROGUEM124)(RogueOptionalByte);
typedef RogueOptionalByte(*ROGUEM125)();
typedef void*(*ROGUEM126)(RogueOptionalValue);
typedef void*(*ROGUEM127)(RogueOptionalStringBuilder);
typedef void*(*ROGUEM128)(RogueOptional_Function___);
typedef void*(*ROGUEM129)(RogueOptionalString);
typedef void*(*ROGUEM130)(RogueOptionalPropertyInfo);
typedef void*(*ROGUEM131)(RogueOptionalMethodInfo);
typedef void*(*ROGUEM132)(RogueOptionalCharacter);
typedef RogueOptionalCharacter(*ROGUEM133)();
typedef void*(*ROGUEM134)(RogueOptionalReal64);
typedef void*(*ROGUEM135)(RogueOptionalReal32);
typedef void*(*ROGUEM136)(RogueOptionalInt64);
typedef void*(*ROGUEM137)(RogueClassFileOptions);
typedef void*(*ROGUEM138)(RogueOptionalTableEntry_String_Value_);
typedef void*(*ROGUEM139)(RogueOptionalTypeInfo);
typedef void*(*ROGUEM140)(RogueOptionalTableEntry_Int32_String_);
typedef void*(*ROGUEM141)(RogueOptionalTableEntry_String_String_);
typedef void*(*ROGUEM142)(RogueOptionalTableEntry_String_TypeInfo_);

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

// Value.to_Int64()
extern RogueInt64 Rogue_call_ROGUEM2( int i, void* THIS )
{
  return ((ROGUEM2)(((RogueObject*)THIS)->type->methods[i]))( THIS );
}

// Value.remove(Value)
extern void* Rogue_call_ROGUEM3( int i, void* THIS, void* p0 )
{
  return ((ROGUEM3)(((RogueObject*)THIS)->type->methods[i]))( THIS, p0 );
}

// Value.set(String,Value)
extern void* Rogue_call_ROGUEM5( int i, void* THIS, void* p0, void* p1 )
{
  return ((ROGUEM5)(((RogueObject*)THIS)->type->methods[i]))( THIS, p0, p1 );
}

// Value.operator==(Value)
extern RogueLogical Rogue_call_ROGUEM6( int i, void* THIS, void* p0 )
{
  return ((ROGUEM6)(((RogueObject*)THIS)->type->methods[i]))( THIS, p0 );
}

// Value.to_Int32()
extern RogueInt32 Rogue_call_ROGUEM7( int i, void* THIS )
{
  return ((ROGUEM7)(((RogueObject*)THIS)->type->methods[i]))( THIS );
}

// Value.get(Int32)
extern void* Rogue_call_ROGUEM8( int i, void* THIS, RogueInt32 p0 )
{
  return ((ROGUEM8)(((RogueObject*)THIS)->type->methods[i]))( THIS, p0 );
}

// Value.is_int32()
extern RogueLogical Rogue_call_ROGUEM9( int i, void* THIS )
{
  return ((ROGUEM9)(((RogueObject*)THIS)->type->methods[i]))( THIS );
}

// Value.to_Character()
extern RogueCharacter Rogue_call_ROGUEM12( int i, void* THIS )
{
  return ((ROGUEM12)(((RogueObject*)THIS)->type->methods[i]))( THIS );
}

// Value.to_Real64()
extern RogueReal64 Rogue_call_ROGUEM13( int i, void* THIS )
{
  return ((ROGUEM13)(((RogueObject*)THIS)->type->methods[i]))( THIS );
}

// Value.to_json(StringBuilder,Int32)
extern void* Rogue_call_ROGUEM17( int i, void* THIS, void* p0, RogueInt32 p1 )
{
  return ((ROGUEM17)(((RogueObject*)THIS)->type->methods[i]))( THIS, p0, p1 );
}

// (Function(TableEntry<<String,Value>>,TableEntry<<String,Value>>)->Logical).call(TableEntry<<String,Value>>,TableEntry<<String,Value>>)
extern RogueLogical Rogue_call_ROGUEM42( int i, void* THIS, void* p0, void* p1 )
{
  return ((ROGUEM42)(((RogueObject*)THIS)->type->methods[i]))( THIS, p0, p1 );
}


// GLOBAL PROPERTIES
RogueByte_List* RogueStringBuilder_work_bytes = 0;
RogueClassStringBuilderPool* RogueStringBuilder_pool = 0;
RogueClassTable_Int32_String_* RogueMethodInfo__method_name_strings = 0;
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
void RogueTypeInfo_trace( void* obj );
void RoguePropertyInfo_List_trace( void* obj );
void RogueMethodInfo_List_trace( void* obj );
void RogueMethodInfo_trace( void* obj );
void RogueTable_Int32_String__trace( void* obj );
void RogueTableEntry_Int32_String__trace( void* obj );
void RogueCharacter_List_trace( void* obj );
void RogueValueList_trace( void* obj );
void RogueValue_List_trace( void* obj );
void RogueObjectValue_trace( void* obj );
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
void RogueTableEntry_String_TypeInfo__trace( void* obj );
void RogueRequirementError_trace( void* obj );
void RogueListRewriter_Character__trace( void* obj );
void RogueFunction_1182_trace( void* obj );
void RogueIOError_trace( void* obj );
void RogueFileReader_trace( void* obj );
void RogueUTF8Reader_trace( void* obj );
void RogueJSONParseError_trace( void* obj );
void RogueJSONParserBuffer_trace( void* obj );
void RogueOptionalBoxed_Value__trace( void* obj );
void RogueOptionalBoxed_StringBuilder__trace( void* obj );
void RogueOptionalBoxed__Function_____trace( void* obj );
void RogueOptionalBoxed_String__trace( void* obj );
void RogueOptionalBoxed_PropertyInfo__trace( void* obj );
void RogueOptionalBoxed_MethodInfo__trace( void* obj );
void RogueOptionalBoxed_TableEntry_String_Value___trace( void* obj );
void RogueOptionalBoxed_TypeInfo__trace( void* obj );
void RogueOptionalBoxed_TableEntry_Int32_String___trace( void* obj );
void RogueOptionalBoxed_TableEntry_String_String___trace( void* obj );
void RogueOptionalBoxed_TableEntry_String_TypeInfo___trace( void* obj );
void RogueOptionalValue_trace( void* obj );
void RogueOptionalStringBuilder_trace( void* obj );
void RogueOptional_Function____trace( void* obj );
void RogueOptionalString_trace( void* obj );
void RogueOptionalPropertyInfo_trace( void* obj );
void RogueOptionalMethodInfo_trace( void* obj );
void RogueOptionalTableEntry_String_Value__trace( void* obj );
void RogueOptionalTypeInfo_trace( void* obj );
void RogueOptionalTableEntry_Int32_String__trace( void* obj );
void RogueOptionalTableEntry_String_String__trace( void* obj );
void RogueOptionalTableEntry_String_TypeInfo__trace( void* obj );

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

void RogueTypeInfo_trace( void* obj )
{
  void* link;
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  if ((link=((RogueTypeInfo*)obj)->name)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=((RogueTypeInfo*)obj)->global_properties)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=((RogueTypeInfo*)obj)->properties)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=((RogueTypeInfo*)obj)->methods)) ((RogueObject*)link)->type->trace_fn( link );
}

void RoguePropertyInfo_List_trace( void* obj )
{
  void* link;
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  if ((link=((RoguePropertyInfo_List*)obj)->data)) RogueArray_trace( link );
}

void RogueMethodInfo_List_trace( void* obj )
{
  void* link;
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  if ((link=((RogueMethodInfo_List*)obj)->data)) RogueArray_trace( link );
}

void RogueMethodInfo_trace( void* obj )
{
  void* link;
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  if ((link=((RogueClassMethodInfo*)obj)->name)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=((RogueClassMethodInfo*)obj)->base_name)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=((RogueClassMethodInfo*)obj)->signature)) ((RogueObject*)link)->type->trace_fn( link );
}

void RogueTable_Int32_String__trace( void* obj )
{
  void* link;
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  if ((link=((RogueClassTable_Int32_String_*)obj)->bins)) RogueArray_trace( link );
  if ((link=((RogueClassTable_Int32_String_*)obj)->first_entry)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=((RogueClassTable_Int32_String_*)obj)->last_entry)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=((RogueClassTable_Int32_String_*)obj)->cur_entry)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=((RogueClassTable_Int32_String_*)obj)->sort_function)) ((RogueObject*)link)->type->trace_fn( link );
}

void RogueTableEntry_Int32_String__trace( void* obj )
{
  void* link;
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  if ((link=((RogueClassTableEntry_Int32_String_*)obj)->value)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=((RogueClassTableEntry_Int32_String_*)obj)->adjacent_entry)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=((RogueClassTableEntry_Int32_String_*)obj)->next_entry)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=((RogueClassTableEntry_Int32_String_*)obj)->previous_entry)) ((RogueObject*)link)->type->trace_fn( link );
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

void RogueObjectValue_trace( void* obj )
{
  void* link;
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  if ((link=((RogueClassObjectValue*)obj)->value)) ((RogueObject*)link)->type->trace_fn( link );
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

void RogueTableEntry_String_TypeInfo__trace( void* obj )
{
  void* link;
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  if ((link=((RogueClassTableEntry_String_TypeInfo_*)obj)->key)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=((RogueClassTableEntry_String_TypeInfo_*)obj)->value)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=((RogueClassTableEntry_String_TypeInfo_*)obj)->adjacent_entry)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=((RogueClassTableEntry_String_TypeInfo_*)obj)->next_entry)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=((RogueClassTableEntry_String_TypeInfo_*)obj)->previous_entry)) ((RogueObject*)link)->type->trace_fn( link );
}

void RogueRequirementError_trace( void* obj )
{
  void* link;
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  if ((link=((RogueClassRequirementError*)obj)->message)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=((RogueClassRequirementError*)obj)->stack_trace)) ((RogueObject*)link)->type->trace_fn( link );
}

void RogueListRewriter_Character__trace( void* obj )
{
  void* link;
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  if ((link=((RogueClassListRewriter_Character_*)obj)->list)) ((RogueObject*)link)->type->trace_fn( link );
}

void RogueFunction_1182_trace( void* obj )
{
  void* link;
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  if ((link=((RogueClassFunction_1182*)obj)->console)) ((RogueObject*)link)->type->trace_fn( link );
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

void RogueOptionalBoxed_Value__trace( void* obj )
{
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  RogueOptionalValue_trace( &((RogueClassOptionalBoxed_Value_*)obj)->value );
}

void RogueOptionalBoxed_StringBuilder__trace( void* obj )
{
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  RogueOptionalStringBuilder_trace( &((RogueClassOptionalBoxed_StringBuilder_*)obj)->value );
}

void RogueOptionalBoxed__Function_____trace( void* obj )
{
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  RogueOptional_Function____trace( &((RogueClassOptionalBoxed__Function____*)obj)->value );
}

void RogueOptionalBoxed_String__trace( void* obj )
{
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  RogueOptionalString_trace( &((RogueClassOptionalBoxed_String_*)obj)->value );
}

void RogueOptionalBoxed_PropertyInfo__trace( void* obj )
{
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  RogueOptionalPropertyInfo_trace( &((RogueClassOptionalBoxed_PropertyInfo_*)obj)->value );
}

void RogueOptionalBoxed_MethodInfo__trace( void* obj )
{
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  RogueOptionalMethodInfo_trace( &((RogueClassOptionalBoxed_MethodInfo_*)obj)->value );
}

void RogueOptionalBoxed_TableEntry_String_Value___trace( void* obj )
{
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  RogueOptionalTableEntry_String_Value__trace( &((RogueClassOptionalBoxed_TableEntry_String_Value__*)obj)->value );
}

void RogueOptionalBoxed_TypeInfo__trace( void* obj )
{
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  RogueOptionalTypeInfo_trace( &((RogueClassOptionalBoxed_TypeInfo_*)obj)->value );
}

void RogueOptionalBoxed_TableEntry_Int32_String___trace( void* obj )
{
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  RogueOptionalTableEntry_Int32_String__trace( &((RogueClassOptionalBoxed_TableEntry_Int32_String__*)obj)->value );
}

void RogueOptionalBoxed_TableEntry_String_String___trace( void* obj )
{
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  RogueOptionalTableEntry_String_String__trace( &((RogueClassOptionalBoxed_TableEntry_String_String__*)obj)->value );
}

void RogueOptionalBoxed_TableEntry_String_TypeInfo___trace( void* obj )
{
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
  RogueOptionalTableEntry_String_TypeInfo__trace( &((RogueClassOptionalBoxed_TableEntry_String_TypeInfo__*)obj)->value );
}

void RogueOptionalValue_trace( void* obj )
{
  void* link;
  if ((link=((RogueOptionalValue*)obj)->value)) ((RogueObject*)link)->type->trace_fn( link );
}

void RogueOptionalStringBuilder_trace( void* obj )
{
  void* link;
  if ((link=((RogueOptionalStringBuilder*)obj)->value)) ((RogueObject*)link)->type->trace_fn( link );
}

void RogueOptional_Function____trace( void* obj )
{
  void* link;
  if ((link=((RogueOptional_Function___*)obj)->value)) ((RogueObject*)link)->type->trace_fn( link );
}

void RogueOptionalString_trace( void* obj )
{
  void* link;
  if ((link=((RogueOptionalString*)obj)->value)) ((RogueObject*)link)->type->trace_fn( link );
}

void RogueOptionalPropertyInfo_trace( void* obj )
{
  void* link;
  if ((link=((RogueOptionalPropertyInfo*)obj)->value)) ((RogueObject*)link)->type->trace_fn( link );
}

void RogueOptionalMethodInfo_trace( void* obj )
{
  void* link;
  if ((link=((RogueOptionalMethodInfo*)obj)->value)) ((RogueObject*)link)->type->trace_fn( link );
}

void RogueOptionalTableEntry_String_Value__trace( void* obj )
{
  void* link;
  if ((link=((RogueOptionalTableEntry_String_Value_*)obj)->value)) ((RogueObject*)link)->type->trace_fn( link );
}

void RogueOptionalTypeInfo_trace( void* obj )
{
  void* link;
  if ((link=((RogueOptionalTypeInfo*)obj)->value)) ((RogueObject*)link)->type->trace_fn( link );
}

void RogueOptionalTableEntry_Int32_String__trace( void* obj )
{
  void* link;
  if ((link=((RogueOptionalTableEntry_Int32_String_*)obj)->value)) ((RogueObject*)link)->type->trace_fn( link );
}

void RogueOptionalTableEntry_String_String__trace( void* obj )
{
  void* link;
  if ((link=((RogueOptionalTableEntry_String_String_*)obj)->value)) ((RogueObject*)link)->type->trace_fn( link );
}

void RogueOptionalTableEntry_String_TypeInfo__trace( void* obj )
{
  void* link;
  if ((link=((RogueOptionalTableEntry_String_TypeInfo_*)obj)->value)) ((RogueObject*)link)->type->trace_fn( link );
}


const int Rogue_type_name_index_table[] =
{
  9,138,255,256,178,139,157,257,199,144,164,140,165,142,208,143,
  258,198,162,214,200,259,213,150,197,4,147,212,196,260,261,262,
  263,141,211,145,195,215,166,167,201,168,169,202,209,203,177,210,
  204,232,226,219,146,205,170,171,264,265,148,266,159,206,149,231,
  216,151,218,152,153,267,154,268,155,156,158,160,224,161,163,172,
  269,179,233,234,180,181,182,225,220,176,221,173,235,217,236,222,
  174,175,223,207,237,238,239,240,241,242,243,244,245,246,247,248,
  249,250,251,252,253,270,271,272,273,274,275,276,277,278,279,280,
  281,282,283,284,285,286,287,288
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
  0,
  (RogueInitFn) RogueTypeInfo__init_object,
  (RogueInitFn) RoguePropertyInfo_List__init_object,
  (RogueInitFn) RoguePropertyInfo__init_object,
  0,
  (RogueInitFn) RogueMethodInfo_List__init_object,
  (RogueInitFn) RogueMethodInfo__init_object,
  (RogueInitFn) RogueTable_Int32_String___init_object,
  0,
  (RogueInitFn) RogueTableEntry_Int32_String___init_object,
  (RogueInitFn) Rogue_Function_TableEntry_Int32_String__TableEntry_Int32_String__RETURNSLogical___init_object,
  0,
  (RogueInitFn) RogueCharacter_List__init_object,
  0,
  (RogueInitFn) RogueValueList__init_object,
  (RogueInitFn) RogueValue_List__init_object,
  0,
  (RogueInitFn) RogueObjectValue__init_object,
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
  (RogueInitFn) RogueBoxed__init_object,
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
  (RogueInitFn) RogueFunction_281__init_object,
  (RogueInitFn) RogueRuntime__init_object,
  (RogueInitFn) RogueWeakReference__init_object,
  (RogueInitFn) RoguePrintWriterAdapter__init_object,
  0,
  (RogueInitFn) RogueLogicalValue__init_object,
  (RogueInitFn) RogueInt32Value__init_object,
  (RogueInitFn) RogueInt64Value__init_object,
  (RogueInitFn) RogueReal64Value__init_object,
  (RogueInitFn) RogueNullValue__init_object,
  (RogueInitFn) RogueStringValue__init_object,
  (RogueInitFn) RogueUndefinedValue__init_object,
  (RogueInitFn) RogueOutOfBoundsError__init_object,
  (RogueInitFn) RogueTableEntry_String_TypeInfo___init_object,
  (RogueInitFn) RogueRequirementError__init_object,
  (RogueInitFn) RogueListRewriter_Character___init_object,
  (RogueInitFn) RogueOptionalBoxed_Int32___init_object,
  (RogueInitFn) RogueFunction_1182__init_object,
  (RogueInitFn) RogueBoxed_SystemEnvironment___init_object,
  (RogueInitFn) RogueIOError__init_object,
  (RogueInitFn) RogueFileReader__init_object,
  (RogueInitFn) RogueUTF8Reader__init_object,
  (RogueInitFn) RogueJSONParseError__init_object,
  (RogueInitFn) RogueJSONParserBuffer__init_object,
  (RogueInitFn) RogueOptionalBoxed_Byte___init_object,
  (RogueInitFn) RogueOptionalBoxed_Value___init_object,
  (RogueInitFn) RogueOptionalBoxed_StringBuilder___init_object,
  (RogueInitFn) RogueOptionalBoxed__Function______init_object,
  (RogueInitFn) RogueOptionalBoxed_String___init_object,
  (RogueInitFn) RogueOptionalBoxed_PropertyInfo___init_object,
  (RogueInitFn) RogueOptionalBoxed_MethodInfo___init_object,
  (RogueInitFn) RogueOptionalBoxed_Character___init_object,
  (RogueInitFn) RogueOptionalBoxed_Real64___init_object,
  (RogueInitFn) RogueOptionalBoxed_Real32___init_object,
  (RogueInitFn) RogueOptionalBoxed_Int64___init_object,
  (RogueInitFn) RogueBoxed_FileOptions___init_object,
  (RogueInitFn) RogueOptionalBoxed_TableEntry_String_Value____init_object,
  (RogueInitFn) RogueOptionalBoxed_TypeInfo___init_object,
  (RogueInitFn) RogueOptionalBoxed_TableEntry_Int32_String____init_object,
  (RogueInitFn) RogueOptionalBoxed_TableEntry_String_String____init_object,
  (RogueInitFn) RogueOptionalBoxed_TableEntry_String_TypeInfo____init_object,
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
  0,
  (RogueInitFn) RogueObject__init,
  (RogueInitFn) RoguePropertyInfo_List__init,
  (RogueInitFn) RogueObject__init,
  0,
  (RogueInitFn) RogueMethodInfo_List__init,
  (RogueInitFn) RogueObject__init,
  (RogueInitFn) RogueTable_Int32_String___init,
  0,
  (RogueInitFn) RogueObject__init,
  (RogueInitFn) RogueObject__init,
  0,
  (RogueInitFn) RogueCharacter_List__init,
  0,
  (RogueInitFn) RogueValueList__init,
  (RogueInitFn) RogueValue_List__init,
  0,
  (RogueInitFn) RogueObject__init,
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
  (RogueInitFn) RogueObject__init,
  (RogueInitFn) RogueObject__init,
  (RogueInitFn) RogueException__init,
  (RogueInitFn) RogueObject__init,
  (RogueInitFn) RogueException__init,
  (RogueInitFn) RogueObject__init,
  (RogueInitFn) RogueObject__init,
  (RogueInitFn) RogueObject__init,
  (RogueInitFn) RogueObject__init,
  (RogueInitFn) RogueException__init,
  (RogueInitFn) RogueObject__init,
  (RogueInitFn) RogueObject__init,
  (RogueInitFn) RogueException__init,
  (RogueInitFn) RogueStringBuilder__init,
  (RogueInitFn) RogueObject__init,
  (RogueInitFn) RogueObject__init,
  (RogueInitFn) RogueObject__init,
  (RogueInitFn) RogueObject__init,
  (RogueInitFn) RogueObject__init,
  (RogueInitFn) RogueObject__init,
  (RogueInitFn) RogueObject__init,
  (RogueInitFn) RogueObject__init,
  (RogueInitFn) RogueObject__init,
  (RogueInitFn) RogueObject__init,
  (RogueInitFn) RogueObject__init,
  (RogueInitFn) RogueObject__init,
  (RogueInitFn) RogueObject__init,
  (RogueInitFn) RogueObject__init,
  (RogueInitFn) RogueObject__init,
  (RogueInitFn) RogueObject__init,
  (RogueInitFn) RogueObject__init,
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
  0,
  (RogueToStringFn) RogueTypeInfo__to_String,
  (RogueToStringFn) RoguePropertyInfo_List__to_String,
  (RogueToStringFn) RoguePropertyInfo__to_String,
  0,
  (RogueToStringFn) RogueMethodInfo_List__to_String,
  (RogueToStringFn) RogueMethodInfo__to_String,
  (RogueToStringFn) RogueTable_Int32_String___to_String,
  0,
  (RogueToStringFn) RogueTableEntry_Int32_String___to_String,
  (RogueToStringFn) RogueObject__to_String,
  0,
  (RogueToStringFn) RogueCharacter_List__to_String,
  0,
  (RogueToStringFn) RogueValueList__to_String,
  (RogueToStringFn) RogueValue_List__to_String,
  0,
  (RogueToStringFn) RogueObjectValue__to_String,
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
  (RogueToStringFn) RogueInt32Value__to_String,
  (RogueToStringFn) RogueInt64Value__to_String,
  (RogueToStringFn) RogueReal64Value__to_String,
  (RogueToStringFn) RogueNullValue__to_String,
  (RogueToStringFn) RogueStringValue__to_String,
  (RogueToStringFn) RogueUndefinedValue__to_String,
  (RogueToStringFn) RogueException__to_String,
  (RogueToStringFn) RogueTableEntry_String_TypeInfo___to_String,
  (RogueToStringFn) RogueException__to_String,
  (RogueToStringFn) RogueObject__to_String,
  (RogueToStringFn) RogueOptionalBoxed_Int32___to_String,
  (RogueToStringFn) RogueObject__to_String,
  (RogueToStringFn) RogueBoxed_SystemEnvironment___to_String,
  (RogueToStringFn) RogueException__to_String,
  (RogueToStringFn) RogueObject__to_String,
  (RogueToStringFn) RogueObject__to_String,
  (RogueToStringFn) RogueException__to_String,
  (RogueToStringFn) RogueStringBuilder__to_String,
  (RogueToStringFn) RogueOptionalBoxed_Byte___to_String,
  (RogueToStringFn) RogueOptionalBoxed_Value___to_String,
  (RogueToStringFn) RogueOptionalBoxed_StringBuilder___to_String,
  (RogueToStringFn) RogueOptionalBoxed__Function______to_String,
  (RogueToStringFn) RogueOptionalBoxed_String___to_String,
  (RogueToStringFn) RogueOptionalBoxed_PropertyInfo___to_String,
  (RogueToStringFn) RogueOptionalBoxed_MethodInfo___to_String,
  (RogueToStringFn) RogueOptionalBoxed_Character___to_String,
  (RogueToStringFn) RogueOptionalBoxed_Real64___to_String,
  (RogueToStringFn) RogueOptionalBoxed_Real32___to_String,
  (RogueToStringFn) RogueOptionalBoxed_Int64___to_String,
  (RogueToStringFn) RogueBoxed_FileOptions___to_String,
  (RogueToStringFn) RogueOptionalBoxed_TableEntry_String_Value____to_String,
  (RogueToStringFn) RogueOptionalBoxed_TypeInfo___to_String,
  (RogueToStringFn) RogueOptionalBoxed_TableEntry_Int32_String____to_String,
  (RogueToStringFn) RogueOptionalBoxed_TableEntry_String_String____to_String,
  (RogueToStringFn) RogueOptionalBoxed_TableEntry_String_TypeInfo____to_String,
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
  0,
  RogueTypeInfo_trace,
  RoguePropertyInfo_List_trace,
  RogueObject_trace,
  RogueObject_trace,
  RogueMethodInfo_List_trace,
  RogueMethodInfo_trace,
  RogueTable_Int32_String__trace,
  RogueArray_trace,
  RogueTableEntry_Int32_String__trace,
  RogueObject_trace,
  RogueArray_trace,
  RogueCharacter_List_trace,
  RogueObject_trace,
  RogueValueList_trace,
  RogueValue_List_trace,
  RogueObject_trace,
  RogueObjectValue_trace,
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
  RogueObject_trace,
  RogueObject_trace,
  RogueStringValue_trace,
  RogueObject_trace,
  RogueOutOfBoundsError_trace,
  RogueTableEntry_String_TypeInfo__trace,
  RogueRequirementError_trace,
  RogueListRewriter_Character__trace,
  RogueObject_trace,
  RogueFunction_1182_trace,
  RogueObject_trace,
  RogueIOError_trace,
  RogueFileReader_trace,
  RogueUTF8Reader_trace,
  RogueJSONParseError_trace,
  RogueJSONParserBuffer_trace,
  RogueObject_trace,
  RogueOptionalBoxed_Value__trace,
  RogueOptionalBoxed_StringBuilder__trace,
  RogueOptionalBoxed__Function_____trace,
  RogueOptionalBoxed_String__trace,
  RogueOptionalBoxed_PropertyInfo__trace,
  RogueOptionalBoxed_MethodInfo__trace,
  RogueObject_trace,
  RogueObject_trace,
  RogueObject_trace,
  RogueObject_trace,
  RogueObject_trace,
  RogueOptionalBoxed_TableEntry_String_Value___trace,
  RogueOptionalBoxed_TypeInfo__trace,
  RogueOptionalBoxed_TableEntry_Int32_String___trace,
  RogueOptionalBoxed_TableEntry_String_String___trace,
  RogueOptionalBoxed_TableEntry_String_TypeInfo___trace,
  0,
  0,
  0,
  RogueOptionalValue_trace,
  RogueOptionalStringBuilder_trace,
  RogueOptional_Function____trace,
  RogueOptionalString_trace,
  RogueOptionalPropertyInfo_trace,
  RogueOptionalMethodInfo_trace,
  0,
  0,
  0,
  0,
  0,
  RogueOptionalTableEntry_String_Value__trace,
  RogueOptionalTypeInfo_trace,
  RogueOptionalTableEntry_Int32_String__trace,
  RogueOptionalTableEntry_String_String__trace,
  RogueOptionalTableEntry_String_TypeInfo__trace
};

void Rogue_trace()
{
  void* link;
  int i;

  // Trace GLOBAL PROPERTIES
  if ((link=RogueStringBuilder_work_bytes)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=RogueStringBuilder_pool)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=RogueMethodInfo__method_name_strings)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=RogueSystem_command_line_arguments)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=RogueSystem_executable_filepath)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=RogueLogicalValue_true_value)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=RogueLogicalValue_false_value)) ((RogueObject*)link)->type->trace_fn( link );
  if ((link=RogueStringValue_empty_string)) ((RogueObject*)link)->type->trace_fn( link );

  // Trace Class objects and singletons
  for (i=Rogue_type_count; --i>=0; )
  {
    RogueType* type = &Rogue_types[i];
    if (type->type_info) RogueTypeInfo_trace( type->type_info );
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
  (void*) (ROGUEM2) RogueObject__object_id,
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
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueGlobal__type_name,
  (void*) (ROGUEM1) RogueGlobal__close,
  (void*) (ROGUEM1) RogueGlobal__flush,
  (void*) (ROGUEM3) RogueGlobal__print__Object,
  (void*) (ROGUEM3) RogueGlobal__print__String,
  (void*) (ROGUEM1) RogueGlobal__println,
  (void*) (ROGUEM3) RogueGlobal__println__Object,
  (void*) (ROGUEM3) RogueGlobal__println__String,
  (void*) (ROGUEM3) RogueGlobal__write__StringBuilder,
  (void*) (ROGUEM3) RogueGlobal__prep_arg__String,
  (void*) (ROGUEM0) RogueGlobal__require_command_line,
  (void*) (ROGUEM0) RogueGlobal__save_cache,
  (void*) (ROGUEM0) RogueGlobal__install_library_manager,
  (void*) (ROGUEM0) RogueGlobal__install_brew,
  (void*) (ROGUEM4) RogueGlobal__install_library__Value,
  (void*) (ROGUEM4) RogueGlobal__install_macos_library__Value,
  (void*) (ROGUEM5) RogueGlobal__find_path__Value_String,
  (void*) (ROGUEM4) RogueGlobal__install_ubuntu_library__Value,
  (void*) (ROGUEM3) RogueGlobal__library_location__Value,
  (void*) (ROGUEM3) RogueGlobal__header_location__Value,
  (void*) (ROGUEM4) RogueGlobal__scan_config__File,
  (void*) (ROGUEM3) RogueGlobal__parse_filepath__JSONParser,
  (void*) (ROGUEM0) RogueGlobal__on_launch,
  (void*) (ROGUEM0) RogueGlobal__run_tests,
  (void*) (ROGUEM0) RogueGlobal__call_exit_functions,
  (void*) (ROGUEM4) RogueGlobal__on_exit___Function___,
  (void*) (ROGUEM1) RoguePrintWriter_global_output_buffer___close, // PrintWriter<<global_output_buffer>>
  (void*) (ROGUEM1) RoguePrintWriter_global_output_buffer___flush,
  0, // PrintWriter<<global_output_buffer>>.on_use()
  0, // PrintWriter<<global_output_buffer>>.on_end_use(Exception)
  0, // PrintWriter<<global_output_buffer>>.print(Byte)
  0, // PrintWriter<<global_output_buffer>>.print(Character)
  0, // PrintWriter<<global_output_buffer>>.print(Int32)
  0, // PrintWriter<<global_output_buffer>>.print(Logical)
  0, // PrintWriter<<global_output_buffer>>.print(Int64)
  0, // PrintWriter<<global_output_buffer>>.print(Object)
  0, // PrintWriter<<global_output_buffer>>.print(Real64)
  0, // PrintWriter<<global_output_buffer>>.print(Real64,Int32)
  0, // PrintWriter<<global_output_buffer>>.print(Real32)
  0, // PrintWriter<<global_output_buffer>>.print(String)
  0, // PrintWriter<<global_output_buffer>>.println()
  0, // PrintWriter<<global_output_buffer>>.println(Byte)
  0, // PrintWriter<<global_output_buffer>>.println(Character)
  0, // PrintWriter<<global_output_buffer>>.println(Int32)
  0, // PrintWriter<<global_output_buffer>>.println(Logical)
  0, // PrintWriter<<global_output_buffer>>.println(Int64)
  0, // PrintWriter<<global_output_buffer>>.println(Real64)
  0, // PrintWriter<<global_output_buffer>>.println(Real64,Int32)
  0, // PrintWriter<<global_output_buffer>>.println(Real32)
  0, // PrintWriter<<global_output_buffer>>.println(Object)
  (void*) (ROGUEM3) RoguePrintWriter_global_output_buffer___println__String,
  0, // PrintWriter<<global_output_buffer>>.write(StringBuilder)
  0, // PrintWriter.close() // PrintWriter
  0, // PrintWriter.flush()
  0, // PrintWriter.on_use()
  0, // PrintWriter.on_end_use(Exception)
  0, // PrintWriter.print(Byte)
  0, // PrintWriter.print(Character)
  0, // PrintWriter.print(Int32)
  0, // PrintWriter.print(Logical)
  0, // PrintWriter.print(Int64)
  0, // PrintWriter.print(Object)
  0, // PrintWriter.print(Real64)
  0, // PrintWriter.print(Real64,Int32)
  0, // PrintWriter.print(Real32)
  0, // PrintWriter.print(String)
  0, // PrintWriter.println()
  0, // PrintWriter.println(Byte)
  0, // PrintWriter.println(Character)
  0, // PrintWriter.println(Int32)
  0, // PrintWriter.println(Logical)
  0, // PrintWriter.println(Int64)
  0, // PrintWriter.println(Real64)
  0, // PrintWriter.println(Real64,Int32)
  0, // PrintWriter.println(Real32)
  0, // PrintWriter.println(Object)
  0, // PrintWriter.println(String)
  0, // PrintWriter.write(StringBuilder)
  (void*) (ROGUEM1) RogueValueTable__init_object, // ValueTable
  (void*) (ROGUEM1) RogueValueTable__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
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
  (void*) (ROGUEM6) RogueValueTable__contains__String,
  (void*) (ROGUEM6) RogueValueTable__contains__Value,
  0, // Value.contains((Function(Value)->Logical))
  (void*) (ROGUEM7) RogueValueTable__count,
  0, // ValueTable.count((Function(Value)->Logical))
  0, // Value.compressed()
  0, // Value.decompressed()
  0, // ValueTable.decode_indexed(ValueIDLookupTable)
  0, // ValueTable.encode_indexed(ValueIDTableBuilder)
  (void*) (ROGUEM3) RogueValueTable__ensure_list__String,
  (void*) (ROGUEM3) RogueValueTable__ensure_table__String,
  0, // Value.first()
  0, // ValueTable.first((Function(Value)->Logical))
  (void*) (ROGUEM8) RogueValueTable__get__Int32,
  (void*) (ROGUEM3) RogueValueTable__get__String,
  0, // ValueTable.get((Function(Value)->Logical))
  0, // Value.insert(Value,Int32)
  0, // Value.insert(Value[],Int32)
  0, // Value.insert_all(Value,Int32)
  (void*) (ROGUEM9) RogueValueTable__is_collection,
  (void*) (ROGUEM9) RogueValue__is_complex,
  (void*) (ROGUEM9) RogueValue__is_int32,
  (void*) (ROGUEM9) RogueValue__is_int64,
  0, // Value.is_list()
  (void*) (ROGUEM9) RogueValue__is_logical,
  (void*) (ROGUEM9) RogueValue__is_null,
  (void*) (ROGUEM9) RogueValue__is_non_null,
  (void*) (ROGUEM9) RogueValue__is_number,
  (void*) (ROGUEM9) RogueValue__is_object,
  0, // Value.is_real64()
  (void*) (ROGUEM9) RogueValue__is_string,
  0, // ValueTable.is_table()
  0, // Value.is_undefined()
  (void*) (ROGUEM1) RogueValueTable__keys,
  0, // Value.last()
  0, // ValueTable.last((Function(Value)->Logical))
  0, // ValueTable.locate(Value)
  0, // ValueTable.locate((Function(Value)->Logical))
  0, // ValueTable.locate_last(Value)
  0, // ValueTable.locate_last((Function(Value)->Logical))
  (void*) (ROGUEM6) RogueValue__operatorEQUALSEQUALS__Value,
  0, // Value.operator==(Byte)
  0, // Value.operator==(Character)
  0, // Value.operator==(Int32)
  0, // Value.operator==(Int64)
  0, // Value.operator==(Logical)
  0, // Value.operator==(Real64)
  0, // Value.operator==(Real32)
  (void*) (ROGUEM6) RogueValue__operatorEQUALSEQUALS__String,
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
  (void*) (ROGUEM3) RogueValue__remove__Value,
  0, // ValueTable.remove(String)
  0, // ValueTable.remove((Function(Value)->Logical))
  0, // ValueTable.remove_at(Int32)
  0, // Value.remove_first()
  0, // Value.remove_last()
  0, // Value.reserve(Int32)
  0, // Value.rest(Value)
  (void*) (ROGUEM10) RogueValue__save__File_Logical_Logical,
  0, // ValueTable.set(Int32,Value)
  (void*) (ROGUEM5) RogueValueTable__set__String_Value,
  0, // ValueTable.sort((Function(Value,Value)->Logical))
  0, // Value.sorted((Function(Value,Value)->Logical))
  (void*) (ROGUEM11) RogueValue__to_Byte,
  (void*) (ROGUEM12) RogueValue__to_Character,
  (void*) (ROGUEM2) RogueValue__to_Int64,
  (void*) (ROGUEM7) RogueValue__to_Int32,
  (void*) (ROGUEM9) RogueValueTable__to_Logical,
  (void*) (ROGUEM13) RogueValue__to_Real64,
  (void*) (ROGUEM14) RogueValue__to_Real32,
  (void*) (ROGUEM1) RogueValue__to_Object,
  0, // Value.to_json(Int32)
  (void*) (ROGUEM15) RogueValue__to_json__Logical_Logical,
  (void*) (ROGUEM16) RogueValue__to_json__StringBuilder_Logical_Logical,
  (void*) (ROGUEM17) RogueValueTable__to_json__StringBuilder_Int32,
  (void*) (ROGUEM1) RogueValue__init_object, // Value
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
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
  (void*) (ROGUEM6) RogueValue__contains__String,
  (void*) (ROGUEM6) RogueValue__contains__Value,
  0, // Value.contains((Function(Value)->Logical))
  (void*) (ROGUEM7) RogueValue__count,
  0, // Value.count((Function(Value)->Logical))
  0, // Value.compressed()
  0, // Value.decompressed()
  0, // Value.decode_indexed(ValueIDLookupTable)
  0, // Value.encode_indexed(ValueIDTableBuilder)
  0, // Value.ensure_list(String)
  (void*) (ROGUEM3) RogueValue__ensure_table__String,
  0, // Value.first()
  0, // Value.first((Function(Value)->Logical))
  (void*) (ROGUEM8) RogueValue__get__Int32,
  (void*) (ROGUEM3) RogueValue__get__String,
  0, // Value.get((Function(Value)->Logical))
  0, // Value.insert(Value,Int32)
  0, // Value.insert(Value[],Int32)
  0, // Value.insert_all(Value,Int32)
  (void*) (ROGUEM9) RogueValue__is_collection,
  (void*) (ROGUEM9) RogueValue__is_complex,
  (void*) (ROGUEM9) RogueValue__is_int32,
  (void*) (ROGUEM9) RogueValue__is_int64,
  0, // Value.is_list()
  (void*) (ROGUEM9) RogueValue__is_logical,
  (void*) (ROGUEM9) RogueValue__is_null,
  (void*) (ROGUEM9) RogueValue__is_non_null,
  (void*) (ROGUEM9) RogueValue__is_number,
  (void*) (ROGUEM9) RogueValue__is_object,
  0, // Value.is_real64()
  (void*) (ROGUEM9) RogueValue__is_string,
  0, // Value.is_table()
  0, // Value.is_undefined()
  0, // Value.keys()
  0, // Value.last()
  0, // Value.last((Function(Value)->Logical))
  0, // Value.locate(Value)
  0, // Value.locate((Function(Value)->Logical))
  0, // Value.locate_last(Value)
  0, // Value.locate_last((Function(Value)->Logical))
  (void*) (ROGUEM6) RogueValue__operatorEQUALSEQUALS__Value,
  0, // Value.operator==(Byte)
  0, // Value.operator==(Character)
  0, // Value.operator==(Int32)
  0, // Value.operator==(Int64)
  0, // Value.operator==(Logical)
  0, // Value.operator==(Real64)
  0, // Value.operator==(Real32)
  (void*) (ROGUEM6) RogueValue__operatorEQUALSEQUALS__String,
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
  (void*) (ROGUEM3) RogueValue__remove__Value,
  0, // Value.remove(String)
  0, // Value.remove((Function(Value)->Logical))
  0, // Value.remove_at(Int32)
  0, // Value.remove_first()
  0, // Value.remove_last()
  0, // Value.reserve(Int32)
  0, // Value.rest(Value)
  (void*) (ROGUEM10) RogueValue__save__File_Logical_Logical,
  0, // Value.set(Int32,Value)
  (void*) (ROGUEM5) RogueValue__set__String_Value,
  0, // Value.sort((Function(Value,Value)->Logical))
  0, // Value.sorted((Function(Value,Value)->Logical))
  (void*) (ROGUEM11) RogueValue__to_Byte,
  (void*) (ROGUEM12) RogueValue__to_Character,
  (void*) (ROGUEM2) RogueValue__to_Int64,
  (void*) (ROGUEM7) RogueValue__to_Int32,
  (void*) (ROGUEM9) RogueValue__to_Logical,
  (void*) (ROGUEM13) RogueValue__to_Real64,
  (void*) (ROGUEM14) RogueValue__to_Real32,
  (void*) (ROGUEM1) RogueValue__to_Object,
  0, // Value.to_json(Int32)
  (void*) (ROGUEM15) RogueValue__to_json__Logical_Logical,
  (void*) (ROGUEM16) RogueValue__to_json__StringBuilder_Logical_Logical,
  (void*) (ROGUEM17) RogueValue__to_json__StringBuilder_Int32,
  (void*) (ROGUEM1) RogueTable_String_Value___init_object, // Table<<String,Value>>
  (void*) (ROGUEM1) RogueTable_String_Value___init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueTable_String_Value___to_String,
  0, // Table<<String,Value>>.to_Value()
  0, // Object.to_ValueList()
  0, // Table<<String,Value>>.to_ValueTable()
  0, // Object.type_info()
  0, // Table<<String,Value>>.unpack(Value)
  (void*) (ROGUEM1) RogueTable_String_Value___type_name,
  (void*) (ROGUEM8) RogueTable_String_Value___init__Int32,
  (void*) (ROGUEM6) RogueTable_String_Value___contains__String,
  (void*) (ROGUEM3) RogueTable_String_Value___find__String,
  (void*) (ROGUEM3) RogueTable_String_Value___get__String,
  (void*) (ROGUEM3) RogueTable_String_Value___keys__String_List,
  (void*) (ROGUEM3) RogueTable_String_Value___print_to__StringBuilder,
  (void*) (ROGUEM5) RogueTable_String_Value___set__String_Value,
  (void*) (ROGUEM4) RogueTable_String_Value____adjust_entry_order__TableEntry_String_Value_,
  (void*) (ROGUEM4) RogueTable_String_Value____place_entry_in_order__TableEntry_String_Value_,
  (void*) (ROGUEM4) RogueTable_String_Value____unlink__TableEntry_String_Value_,
  (void*) (ROGUEM0) RogueTable_String_Value____grow,
  (void*) (ROGUEM25) RogueInt32__hash_code, // Int32
  (void*) (ROGUEM26) RogueInt32__or_smaller__Int32,
  (void*) (ROGUEM21) RogueInt32__to_String,
  (void*) (ROGUEM27) RogueInt32__to_digit,
  (void*) (ROGUEM0) RogueObject__init_object, // Array<<TableEntry<<String,Value>>>>
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueArray_TableEntry_String_Value____type_name,
  (void*) (ROGUEM29) RogueArray__zero__Int32_Int32,
  (void*) (ROGUEM0) RogueObject__init_object, // Array
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueArray__type_name,
  (void*) (ROGUEM29) RogueArray__zero__Int32_Int32,
  (void*) (ROGUEM1) RogueTableEntry_String_Value___init_object, // TableEntry<<String,Value>>
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueTableEntry_String_Value___to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueTableEntry_String_Value___type_name,
  (void*) (ROGUEM30) RogueTableEntry_String_Value___init__String_Value_Int32,
  (void*) (ROGUEM0) RogueObject__init_object, // String
  (void*) (ROGUEM1) RogueObject__init,
  (void*) (ROGUEM7) RogueString__hash_code,
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueString__to_String,
  0, // String.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueString__type_name,
  (void*) (ROGUEM8) RogueString__after__Int32,
  (void*) (ROGUEM31) RogueString__after_first__Character,
  (void*) (ROGUEM3) RogueString__after_first__String,
  (void*) (ROGUEM31) RogueString__after_last__Character,
  (void*) (ROGUEM8) RogueString__before__Int32,
  (void*) (ROGUEM31) RogueString__before_first__Character,
  (void*) (ROGUEM3) RogueString__before_first__String,
  (void*) (ROGUEM31) RogueString__before_last__Character,
  (void*) (ROGUEM32) RogueString__begins_with__Character,
  (void*) (ROGUEM6) RogueString__begins_with__String,
  (void*) (ROGUEM1) RogueString__consolidated,
  (void*) (ROGUEM32) RogueString__contains__Character,
  (void*) (ROGUEM6) RogueString__contains__String,
  (void*) (ROGUEM33) RogueString__contains_at__String_Int32,
  (void*) (ROGUEM32) RogueString__ends_with__Character,
  (void*) (ROGUEM6) RogueString__ends_with__String,
  (void*) (ROGUEM8) RogueString__from__Int32,
  (void*) (ROGUEM34) RogueString__from__Int32_Int32,
  (void*) (ROGUEM31) RogueString__from_first__Character,
  (void*) (ROGUEM12) RogueString__last,
  (void*) (ROGUEM35) RogueString__left_justified__Int32_Character,
  (void*) (ROGUEM8) RogueString__leftmost__Int32,
  (void*) (ROGUEM36) RogueString__locate__Character_OptionalInt32,
  (void*) (ROGUEM37) RogueString__locate__String_OptionalInt32,
  (void*) (ROGUEM36) RogueString__locate_last__Character_OptionalInt32,
  (void*) (ROGUEM31) RogueString__operatorPLUS__Character,
  (void*) (ROGUEM8) RogueString__operatorPLUS__Int32,
  (void*) (ROGUEM6) RogueString__operatorEQUALSEQUALS__String,
  (void*) (ROGUEM38) RogueString__operatorPLUS__Logical,
  (void*) (ROGUEM3) RogueString__operatorPLUS__Object,
  (void*) (ROGUEM39) RogueString__operatorPLUS__Real64,
  (void*) (ROGUEM3) RogueString__operatorPLUS__String,
  (void*) (ROGUEM8) RogueString__pluralized__Int32,
  (void*) (ROGUEM5) RogueString__replacing__String_String,
  (void*) (ROGUEM8) RogueString__rightmost__Int32,
  (void*) (ROGUEM31) RogueString__split__Character,
  (void*) (ROGUEM8) RogueString__times__Int32,
  (void*) (ROGUEM1) RogueString__to_lowercase,
  (void*) (ROGUEM1) RogueString__trimmed,
  (void*) (ROGUEM40) RogueString__word_wrap__Int32_String,
  (void*) (ROGUEM41) RogueString__word_wrap__Int32_StringBuilder_String,
  (void*) (ROGUEM1) Rogue_Function_TableEntry_String_Value__TableEntry_String_Value__RETURNSLogical___init_object, // (Function(TableEntry<<String,Value>>,TableEntry<<String,Value>>)->Logical)
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) Rogue_Function_TableEntry_String_Value__TableEntry_String_Value__RETURNSLogical___type_name,
  (void*) (ROGUEM42) Rogue_Function_TableEntry_String_Value__TableEntry_String_Value__RETURNSLogical___call__TableEntry_String_Value__TableEntry_String_Value_,
  (void*) (ROGUEM1) RogueStringBuilder__init_object, // StringBuilder
  (void*) (ROGUEM1) RogueStringBuilder__init,
  0, // StringBuilder.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueStringBuilder__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueStringBuilder__type_name,
  (void*) (ROGUEM8) RogueStringBuilder__init__Int32,
  0, // StringBuilder.init(String)
  (void*) (ROGUEM1) RogueStringBuilder__clear,
  0, // StringBuilder.consolidated()
  0, // StringBuilder.contains(Character)
  0, // StringBuilder.discard_from(Int32)
  (void*) (ROGUEM43) RogueStringBuilder__get__Int32,
  0, // StringBuilder.insert(Character)
  0, // StringBuilder.insert_spaces(Int32,Int32)
  (void*) (ROGUEM44) RogueStringBuilder__locate__Character,
  0, // StringBuilder.locate_last(Character)
  (void*) (ROGUEM9) RogueStringBuilder__needs_indent,
  0, // StringBuilder.operator==(String)
  0, // StringBuilder.operator==(StringBuilder)
  0, // StringBuilder.print(Byte)
  (void*) (ROGUEM45) RogueStringBuilder__print__Character_Logical,
  (void*) (ROGUEM8) RogueStringBuilder__print__Int32,
  (void*) (ROGUEM38) RogueStringBuilder__print__Logical,
  (void*) (ROGUEM46) RogueStringBuilder__print__Int64,
  (void*) (ROGUEM3) RogueStringBuilder__print__Object,
  (void*) (ROGUEM39) RogueStringBuilder__print__Real64,
  (void*) (ROGUEM47) RogueStringBuilder__print__Real64_Int32,
  (void*) (ROGUEM3) RogueStringBuilder__print__String,
  (void*) (ROGUEM0) RogueStringBuilder__print_indent,
  (void*) (ROGUEM47) RogueStringBuilder__print_to_work_bytes__Real64_Int32,
  (void*) (ROGUEM0) RogueStringBuilder__print_work_bytes,
  (void*) (ROGUEM1) RogueStringBuilder__println,
  0, // StringBuilder.println(Byte)
  0, // StringBuilder.println(Character)
  0, // StringBuilder.println(Int32)
  0, // StringBuilder.println(Logical)
  0, // StringBuilder.println(Int64)
  0, // StringBuilder.println(Real64)
  0, // StringBuilder.println(Real64,Int32)
  0, // StringBuilder.println(Object)
  (void*) (ROGUEM3) RogueStringBuilder__println__String,
  0, // StringBuilder.remove_at(Int32)
  0, // StringBuilder.remove_last()
  (void*) (ROGUEM8) RogueStringBuilder__reserve__Int32,
  (void*) (ROGUEM0) RogueStringBuilder__round_off_work_bytes,
  (void*) (ROGUEM13) RogueStringBuilder__scan_work_bytes,
  (void*) (ROGUEM1) RogueByte_List__init_object, // Byte[]
  (void*) (ROGUEM1) RogueByte_List__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueByte_List__to_String,
  0, // Byte[].to_Value()
  0, // Byte[].to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Byte[].unpack(Value)
  (void*) (ROGUEM1) RogueByte_List__type_name,
  (void*) (ROGUEM8) RogueByte_List__init__Int32,
  (void*) (ROGUEM49) RogueByte_List__add__Byte,
  (void*) (ROGUEM7) RogueByte_List__capacity,
  (void*) (ROGUEM1) RogueByte_List__clear,
  (void*) (ROGUEM8) RogueByte_List__ensure_capacity__Int32,
  (void*) (ROGUEM8) RogueByte_List__expand_to_count__Int32,
  (void*) (ROGUEM8) RogueByte_List__expand_to_include__Int32,
  (void*) (ROGUEM8) RogueByte_List__discard_from__Int32,
  (void*) (ROGUEM50) RogueByte_List__insert__Byte_Int32,
  (void*) (ROGUEM8) RogueByte_List__reserve__Int32,
  (void*) (ROGUEM51) RogueByte_List__remove_at__Int32,
  (void*) (ROGUEM11) RogueByte_List__remove_last,
  (void*) (ROGUEM1) RogueByte_List__reverse,
  (void*) (ROGUEM34) RogueByte_List__reverse__Int32_Int32,
  (void*) (ROGUEM52) RogueByte_List__shift__Int32_OptionalInt32_Int32_OptionalByte,
  (void*) (ROGUEM1) RogueGenericList__init_object, // GenericList
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueGenericList__type_name,
  (void*) (ROGUEM20) RogueByte__to_String, // Byte
  (void*) (ROGUEM0) RogueObject__init_object, // Array<<Byte>>
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueArray_Byte___type_name,
  (void*) (ROGUEM29) RogueArray__zero__Int32_Int32,
  (void*) (ROGUEM1) RogueStringBuilderPool__init_object, // StringBuilderPool
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
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
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueStringBuilder_List__to_String,
  0, // StringBuilder[].to_Value()
  0, // StringBuilder[].to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // StringBuilder[].unpack(Value)
  (void*) (ROGUEM1) RogueStringBuilder_List__type_name,
  (void*) (ROGUEM8) RogueStringBuilder_List__init__Int32,
  (void*) (ROGUEM0) RogueObject__init_object, // Array<<StringBuilder>>
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueArray_StringBuilder___type_name,
  (void*) (ROGUEM29) RogueArray__zero__Int32_Int32,
  (void*) (ROGUEM19) RogueLogical__to_String, // Logical
  (void*) (ROGUEM1) Rogue_Function____List__init_object, // (Function())[]
  (void*) (ROGUEM1) Rogue_Function____List__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) Rogue_Function____List__to_String,
  0, // (Function())[].to_Value()
  0, // (Function())[].to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // (Function())[].unpack(Value)
  (void*) (ROGUEM1) Rogue_Function____List__type_name,
  (void*) (ROGUEM8) Rogue_Function____List__init__Int32,
  (void*) (ROGUEM3) Rogue_Function____List__add___Function___,
  (void*) (ROGUEM7) Rogue_Function____List__capacity,
  (void*) (ROGUEM8) Rogue_Function____List__reserve__Int32,
  (void*) (ROGUEM1) Rogue_Function_____init_object, // (Function())
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
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
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueArray__Function______type_name,
  (void*) (ROGUEM29) RogueArray__zero__Int32_Int32,
  (void*) (ROGUEM1) RogueException__init_object, // Exception
  (void*) (ROGUEM1) RogueException__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueException__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueException__type_name,
  (void*) (ROGUEM3) RogueException__init__String,
  (void*) (ROGUEM0) RogueException__display,
  (void*) (ROGUEM1) RogueException__format,
  (void*) (ROGUEM1) RogueStackTrace__init_object, // StackTrace
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueStackTrace__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueStackTrace__type_name,
  (void*) (ROGUEM8) RogueStackTrace__init__Int32,
  (void*) (ROGUEM0) RogueStackTrace__format,
  (void*) (ROGUEM0) RogueStackTrace__print,
  (void*) (ROGUEM3) RogueStackTrace__print__StringBuilder,
  (void*) (ROGUEM1) RogueString_List__init_object, // String[]
  (void*) (ROGUEM1) RogueString_List__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueString_List__to_String,
  0, // String[].to_Value()
  0, // String[].to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // String[].unpack(Value)
  (void*) (ROGUEM1) RogueString_List__type_name,
  (void*) (ROGUEM8) RogueString_List__init__Int32,
  (void*) (ROGUEM3) RogueString_List__add__String,
  (void*) (ROGUEM7) RogueString_List__capacity,
  (void*) (ROGUEM56) RogueString_List__locate__String,
  (void*) (ROGUEM8) RogueString_List__reserve__Int32,
  (void*) (ROGUEM3) RogueString_List__remove__String,
  (void*) (ROGUEM8) RogueString_List__remove_at__Int32,
  (void*) (ROGUEM3) RogueString_List__join__String,
  (void*) (ROGUEM3) RogueString_List__mapped_String____Function_String_RETURNSString_,
  (void*) (ROGUEM0) RogueObject__init_object, // Array<<String>>
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueArray_String___type_name,
  (void*) (ROGUEM29) RogueArray__zero__Int32_Int32,
  (void*) (ROGUEM28) RogueReal64__decimal_digit_count, // Real64
  (void*) (ROGUEM57) RogueReal64__format__OptionalInt32,
  (void*) (ROGUEM58) RogueReal64__fractional_part,
  (void*) (ROGUEM54) RogueReal64__is_infinite,
  (void*) (ROGUEM54) RogueReal64__is_not_a_number,
  (void*) (ROGUEM24) RogueReal64__to_String,
  (void*) (ROGUEM58) RogueReal64__whole_part,
  (void*) (ROGUEM23) RogueReal32__to_String, // Real32
  (void*) (ROGUEM60) RogueInt64__print_in_power2_base__Int32_Int32_StringBuilder, // Int64
  (void*) (ROGUEM22) RogueInt64__to_String,
  (void*) (ROGUEM61) RogueInt64__to_hex_string__Int32,
  (void*) (ROGUEM63) RogueCharacter__is_identifier__Logical_Logical, // Character
  (void*) (ROGUEM64) RogueCharacter__is_letter,
  (void*) (ROGUEM18) RogueCharacter__to_String,
  (void*) (ROGUEM65) RogueCharacter__to_number__Int32,
  (void*) (ROGUEM1) RogueTypeInfo__init_object, // TypeInfo
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueTypeInfo__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueTypeInfo__type_name,
  (void*) (ROGUEM40) RogueTypeInfo__init__Int32_String,
  (void*) (ROGUEM29) RogueTypeInfo__add_global_property_info__Int32_Int32,
  (void*) (ROGUEM29) RogueTypeInfo__add_property_info__Int32_Int32,
  (void*) (ROGUEM67) RogueTypeInfo__add_method_info__Int32,
  (void*) (ROGUEM1) RoguePropertyInfo_List__init_object, // PropertyInfo[]
  (void*) (ROGUEM1) RoguePropertyInfo_List__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RoguePropertyInfo_List__to_String,
  0, // PropertyInfo[].to_Value()
  0, // PropertyInfo[].to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // PropertyInfo[].unpack(Value)
  (void*) (ROGUEM1) RoguePropertyInfo_List__type_name,
  (void*) (ROGUEM8) RoguePropertyInfo_List__init__Int32,
  (void*) (ROGUEM3) RoguePropertyInfo_List__add__PropertyInfo,
  (void*) (ROGUEM7) RoguePropertyInfo_List__capacity,
  (void*) (ROGUEM8) RoguePropertyInfo_List__reserve__Int32,
  (void*) (ROGUEM1) RoguePropertyInfo__init_object, // PropertyInfo
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RoguePropertyInfo__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RoguePropertyInfo__type_name,
  (void*) (ROGUEM69) RoguePropertyInfo__init__Int32_Int32_Int32,
  (void*) (ROGUEM1) RoguePropertyInfo__name,
  (void*) (ROGUEM1) Rogue_PropertyInfo__type,
  (void*) (ROGUEM0) RogueObject__init_object, // Array<<PropertyInfo>>
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueArray_PropertyInfo___type_name,
  (void*) (ROGUEM29) RogueArray__zero__Int32_Int32,
  (void*) (ROGUEM1) RogueMethodInfo_List__init_object, // MethodInfo[]
  (void*) (ROGUEM1) RogueMethodInfo_List__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueMethodInfo_List__to_String,
  0, // MethodInfo[].to_Value()
  0, // MethodInfo[].to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // MethodInfo[].unpack(Value)
  (void*) (ROGUEM1) RogueMethodInfo_List__type_name,
  (void*) (ROGUEM8) RogueMethodInfo_List__init__Int32,
  (void*) (ROGUEM3) RogueMethodInfo_List__add__MethodInfo,
  (void*) (ROGUEM7) RogueMethodInfo_List__capacity,
  (void*) (ROGUEM8) RogueMethodInfo_List__reserve__Int32,
  (void*) (ROGUEM1) RogueMethodInfo__init_object, // MethodInfo
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueMethodInfo__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueMethodInfo__type_name,
  (void*) (ROGUEM70) RogueMethodInfo__init__Int32_Int32_Int32_Int32_Int32_Int32,
  (void*) (ROGUEM1) RogueMethodInfo__name,
  (void*) (ROGUEM1) Rogue_MethodInfo__return_type,
  (void*) (ROGUEM8) RogueMethodInfo__parameter_name__Int32,
  (void*) (ROGUEM8) RogueMethodInfo__parameter_type__Int32,
  (void*) (ROGUEM1) RogueMethodInfo__signature,
  (void*) (ROGUEM1) RogueTable_Int32_String___init_object, // Table<<Int32,String>>
  (void*) (ROGUEM1) RogueTable_Int32_String___init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueTable_Int32_String___to_String,
  0, // Table<<Int32,String>>.to_Value()
  0, // Object.to_ValueList()
  0, // Table<<Int32,String>>.to_ValueTable()
  0, // Object.type_info()
  0, // Table<<Int32,String>>.unpack(Value)
  (void*) (ROGUEM1) RogueTable_Int32_String___type_name,
  (void*) (ROGUEM8) RogueTable_Int32_String___init__Int32,
  (void*) (ROGUEM8) RogueTable_Int32_String___find__Int32,
  (void*) (ROGUEM8) RogueTable_Int32_String___get__Int32,
  (void*) (ROGUEM3) RogueTable_Int32_String___print_to__StringBuilder,
  (void*) (ROGUEM40) RogueTable_Int32_String___set__Int32_String,
  (void*) (ROGUEM4) RogueTable_Int32_String____adjust_entry_order__TableEntry_Int32_String_,
  (void*) (ROGUEM4) RogueTable_Int32_String____place_entry_in_order__TableEntry_Int32_String_,
  (void*) (ROGUEM4) RogueTable_Int32_String____unlink__TableEntry_Int32_String_,
  (void*) (ROGUEM0) RogueTable_Int32_String____grow,
  (void*) (ROGUEM0) RogueObject__init_object, // Array<<TableEntry<<Int32,String>>>>
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueArray_TableEntry_Int32_String____type_name,
  (void*) (ROGUEM29) RogueArray__zero__Int32_Int32,
  (void*) (ROGUEM1) RogueTableEntry_Int32_String___init_object, // TableEntry<<Int32,String>>
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueTableEntry_Int32_String___to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueTableEntry_Int32_String___type_name,
  (void*) (ROGUEM72) RogueTableEntry_Int32_String___init__Int32_String_Int32,
  (void*) (ROGUEM1) Rogue_Function_TableEntry_Int32_String__TableEntry_Int32_String__RETURNSLogical___init_object, // (Function(TableEntry<<Int32,String>>,TableEntry<<Int32,String>>)->Logical)
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) Rogue_Function_TableEntry_Int32_String__TableEntry_Int32_String__RETURNSLogical___type_name,
  (void*) (ROGUEM42) Rogue_Function_TableEntry_Int32_String__TableEntry_Int32_String__RETURNSLogical___call__TableEntry_Int32_String__TableEntry_Int32_String_,
  (void*) (ROGUEM0) RogueObject__init_object, // Array<<MethodInfo>>
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueArray_MethodInfo___type_name,
  (void*) (ROGUEM29) RogueArray__zero__Int32_Int32,
  (void*) (ROGUEM1) RogueCharacter_List__init_object, // Character[]
  (void*) (ROGUEM1) RogueCharacter_List__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueCharacter_List__to_String,
  0, // Character[].to_Value()
  0, // Character[].to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Character[].unpack(Value)
  (void*) (ROGUEM1) RogueCharacter_List__type_name,
  (void*) (ROGUEM8) RogueCharacter_List__init__Int32,
  (void*) (ROGUEM31) RogueCharacter_List__add__Character,
  (void*) (ROGUEM7) RogueCharacter_List__capacity,
  (void*) (ROGUEM8) RogueCharacter_List__discard_from__Int32,
  (void*) (ROGUEM8) RogueCharacter_List__reserve__Int32,
  (void*) (ROGUEM1) RogueCharacter_List__rewriter,
  (void*) (ROGUEM0) RogueObject__init_object, // Array<<Character>>
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueArray_Character___type_name,
  (void*) (ROGUEM29) RogueArray__zero__Int32_Int32,
  (void*) (ROGUEM1) RogueValueList__init_object, // ValueList
  (void*) (ROGUEM1) RogueValueList__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Value.operator==(Object)
  (void*) (ROGUEM1) RogueValueList__to_String,
  0, // Value.to_Value()
  0, // ValueList.to_ValueList()
  0, // Value.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueValueList__type_name,
  (void*) (ROGUEM3) RogueValueList__add__Value,
  0, // ValueList.add(Value[])
  0, // Value.add_all(Value)
  0, // ValueList.clear()
  0, // ValueList.cloned()
  0, // ValueList.apply((Function(Value)->Value))
  (void*) (ROGUEM6) RogueValueList__contains__String,
  (void*) (ROGUEM6) RogueValueList__contains__Value,
  0, // Value.contains((Function(Value)->Logical))
  (void*) (ROGUEM7) RogueValueList__count,
  0, // ValueList.count((Function(Value)->Logical))
  0, // Value.compressed()
  0, // Value.decompressed()
  0, // ValueList.decode_indexed(ValueIDLookupTable)
  0, // ValueList.encode_indexed(ValueIDTableBuilder)
  0, // Value.ensure_list(String)
  (void*) (ROGUEM3) RogueValue__ensure_table__String,
  0, // ValueList.first()
  0, // ValueList.first((Function(Value)->Logical))
  (void*) (ROGUEM8) RogueValueList__get__Int32,
  (void*) (ROGUEM3) RogueValue__get__String,
  0, // ValueList.get((Function(Value)->Logical))
  0, // ValueList.insert(Value,Int32)
  0, // ValueList.insert(Value[],Int32)
  0, // Value.insert_all(Value,Int32)
  (void*) (ROGUEM9) RogueValueList__is_collection,
  (void*) (ROGUEM9) RogueValue__is_complex,
  (void*) (ROGUEM9) RogueValue__is_int32,
  (void*) (ROGUEM9) RogueValue__is_int64,
  0, // ValueList.is_list()
  (void*) (ROGUEM9) RogueValue__is_logical,
  (void*) (ROGUEM9) RogueValue__is_null,
  (void*) (ROGUEM9) RogueValue__is_non_null,
  (void*) (ROGUEM9) RogueValue__is_number,
  (void*) (ROGUEM9) RogueValue__is_object,
  0, // Value.is_real64()
  (void*) (ROGUEM9) RogueValue__is_string,
  0, // Value.is_table()
  0, // Value.is_undefined()
  0, // Value.keys()
  0, // ValueList.last()
  0, // ValueList.last((Function(Value)->Logical))
  0, // ValueList.locate(Value)
  0, // ValueList.locate((Function(Value)->Logical))
  0, // ValueList.locate_last(Value)
  0, // ValueList.locate_last((Function(Value)->Logical))
  (void*) (ROGUEM6) RogueValue__operatorEQUALSEQUALS__Value,
  0, // Value.operator==(Byte)
  0, // Value.operator==(Character)
  0, // Value.operator==(Int32)
  0, // Value.operator==(Int64)
  0, // Value.operator==(Logical)
  0, // Value.operator==(Real64)
  0, // Value.operator==(Real32)
  (void*) (ROGUEM6) RogueValue__operatorEQUALSEQUALS__String,
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
  (void*) (ROGUEM3) RogueValueList__remove__Value,
  0, // Value.remove(String)
  0, // ValueList.remove((Function(Value)->Logical))
  0, // ValueList.remove_at(Int32)
  0, // ValueList.remove_first()
  0, // ValueList.remove_last()
  0, // ValueList.reserve(Int32)
  0, // Value.rest(Value)
  (void*) (ROGUEM10) RogueValue__save__File_Logical_Logical,
  0, // ValueList.set(Int32,Value)
  (void*) (ROGUEM5) RogueValue__set__String_Value,
  0, // ValueList.sort((Function(Value,Value)->Logical))
  0, // ValueList.sorted((Function(Value,Value)->Logical))
  (void*) (ROGUEM11) RogueValue__to_Byte,
  (void*) (ROGUEM12) RogueValue__to_Character,
  (void*) (ROGUEM2) RogueValue__to_Int64,
  (void*) (ROGUEM7) RogueValue__to_Int32,
  (void*) (ROGUEM9) RogueValueList__to_Logical,
  (void*) (ROGUEM13) RogueValue__to_Real64,
  (void*) (ROGUEM14) RogueValue__to_Real32,
  (void*) (ROGUEM1) RogueValue__to_Object,
  0, // Value.to_json(Int32)
  (void*) (ROGUEM15) RogueValue__to_json__Logical_Logical,
  (void*) (ROGUEM16) RogueValue__to_json__StringBuilder_Logical_Logical,
  (void*) (ROGUEM17) RogueValueList__to_json__StringBuilder_Int32,
  0, // Value.values(Value)
  (void*) (ROGUEM8) RogueValueList__init__Int32,
  (void*) (ROGUEM1) RogueValue_List__init_object, // Value[]
  (void*) (ROGUEM1) RogueValue_List__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueValue_List__to_String,
  0, // Value[].to_Value()
  0, // Value[].to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Value[].unpack(Value)
  (void*) (ROGUEM1) RogueValue_List__type_name,
  (void*) (ROGUEM8) RogueValue_List__init__Int32,
  (void*) (ROGUEM3) RogueValue_List__add__Value,
  (void*) (ROGUEM7) RogueValue_List__capacity,
  (void*) (ROGUEM56) RogueValue_List__locate__Value,
  (void*) (ROGUEM8) RogueValue_List__reserve__Int32,
  (void*) (ROGUEM3) RogueValue_List__remove__Value,
  (void*) (ROGUEM8) RogueValue_List__remove_at__Int32,
  (void*) (ROGUEM0) RogueObject__init_object, // Array<<Value>>
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueArray_Value___type_name,
  (void*) (ROGUEM29) RogueArray__zero__Int32_Int32,
  (void*) (ROGUEM1) RogueObjectValue__init_object, // ObjectValue
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Value.operator==(Object)
  (void*) (ROGUEM1) RogueObjectValue__to_String,
  0, // Value.to_Value()
  0, // Value.to_ValueList()
  0, // ObjectValue.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueObjectValue__type_name,
  0, // Value.add(Value)
  0, // Value.add(Value[])
  0, // Value.add_all(Value)
  0, // Value.clear()
  0, // Value.cloned()
  0, // Value.apply((Function(Value)->Value))
  (void*) (ROGUEM6) RogueValue__contains__String,
  (void*) (ROGUEM6) RogueValue__contains__Value,
  0, // Value.contains((Function(Value)->Logical))
  (void*) (ROGUEM7) RogueValue__count,
  0, // Value.count((Function(Value)->Logical))
  0, // Value.compressed()
  0, // Value.decompressed()
  0, // Value.decode_indexed(ValueIDLookupTable)
  0, // Value.encode_indexed(ValueIDTableBuilder)
  0, // Value.ensure_list(String)
  (void*) (ROGUEM3) RogueValue__ensure_table__String,
  0, // Value.first()
  0, // Value.first((Function(Value)->Logical))
  (void*) (ROGUEM8) RogueValue__get__Int32,
  (void*) (ROGUEM3) RogueValue__get__String,
  0, // Value.get((Function(Value)->Logical))
  0, // Value.insert(Value,Int32)
  0, // Value.insert(Value[],Int32)
  0, // Value.insert_all(Value,Int32)
  (void*) (ROGUEM9) RogueValue__is_collection,
  (void*) (ROGUEM9) RogueValue__is_complex,
  (void*) (ROGUEM9) RogueValue__is_int32,
  (void*) (ROGUEM9) RogueValue__is_int64,
  0, // Value.is_list()
  (void*) (ROGUEM9) RogueValue__is_logical,
  (void*) (ROGUEM9) RogueValue__is_null,
  (void*) (ROGUEM9) RogueValue__is_non_null,
  (void*) (ROGUEM9) RogueValue__is_number,
  (void*) (ROGUEM9) RogueObjectValue__is_object,
  0, // Value.is_real64()
  (void*) (ROGUEM9) RogueValue__is_string,
  0, // Value.is_table()
  0, // Value.is_undefined()
  0, // Value.keys()
  0, // Value.last()
  0, // Value.last((Function(Value)->Logical))
  0, // Value.locate(Value)
  0, // Value.locate((Function(Value)->Logical))
  0, // Value.locate_last(Value)
  0, // Value.locate_last((Function(Value)->Logical))
  (void*) (ROGUEM6) RogueValue__operatorEQUALSEQUALS__Value,
  0, // Value.operator==(Byte)
  0, // Value.operator==(Character)
  0, // Value.operator==(Int32)
  0, // Value.operator==(Int64)
  0, // Value.operator==(Logical)
  0, // Value.operator==(Real64)
  0, // Value.operator==(Real32)
  (void*) (ROGUEM6) RogueValue__operatorEQUALSEQUALS__String,
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
  (void*) (ROGUEM3) RogueValue__remove__Value,
  0, // Value.remove(String)
  0, // Value.remove((Function(Value)->Logical))
  0, // Value.remove_at(Int32)
  0, // Value.remove_first()
  0, // Value.remove_last()
  0, // Value.reserve(Int32)
  0, // Value.rest(Value)
  (void*) (ROGUEM10) RogueValue__save__File_Logical_Logical,
  0, // Value.set(Int32,Value)
  (void*) (ROGUEM5) RogueValue__set__String_Value,
  0, // Value.sort((Function(Value,Value)->Logical))
  0, // Value.sorted((Function(Value,Value)->Logical))
  (void*) (ROGUEM11) RogueValue__to_Byte,
  (void*) (ROGUEM12) RogueValue__to_Character,
  (void*) (ROGUEM2) RogueValue__to_Int64,
  (void*) (ROGUEM7) RogueValue__to_Int32,
  (void*) (ROGUEM9) RogueValue__to_Logical,
  (void*) (ROGUEM13) RogueValue__to_Real64,
  (void*) (ROGUEM14) RogueValue__to_Real32,
  (void*) (ROGUEM1) RogueObjectValue__to_Object,
  0, // Value.to_json(Int32)
  (void*) (ROGUEM15) RogueValue__to_json__Logical_Logical,
  (void*) (ROGUEM16) RogueValue__to_json__StringBuilder_Logical_Logical,
  (void*) (ROGUEM17) RogueObjectValue__to_json__StringBuilder_Int32,
  0, // Value.values(Value)
  (void*) (ROGUEM3) RogueObjectValue__init__Object,
  (void*) (ROGUEM1) RogueStringConsolidationTable__init_object, // StringConsolidationTable
  (void*) (ROGUEM1) RogueTable_String_String___init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueTable_String_String___to_String,
  0, // Table<<String,String>>.to_Value()
  0, // Object.to_ValueList()
  0, // Table<<String,String>>.to_ValueTable()
  0, // Object.type_info()
  0, // Table<<String,String>>.unpack(Value)
  (void*) (ROGUEM1) RogueStringConsolidationTable__type_name,
  (void*) (ROGUEM8) RogueTable_String_String___init__Int32,
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
  (void*) (ROGUEM3) RogueTable_String_String___find__String,
  0, // Table<<String,String>>.first()
  0, // Table<<String,String>>.first((Function(String)->Logical))
  (void*) (ROGUEM3) RogueStringConsolidationTable__get__String,
  0, // Table<<String,String>>.get(String,String)
  0, // Table<<String,String>>.get((Function(String)->Logical))
  0, // Table<<String,String>>.keys(String[])
  0, // Table<<String,String>>.locate((Function(String)->Logical))
  (void*) (ROGUEM3) RogueTable_String_String___print_to__StringBuilder,
  0, // Table<<String,String>>.random()
  0, // Table<<String,String>>.remove(String)
  0, // Table<<String,String>>.remove((Function(String)->Logical))
  0, // Table<<String,String>>.remove(TableEntry<<String,String>>)
  (void*) (ROGUEM5) RogueTable_String_String___set__String_String,
  0, // Table<<String,String>>.set_sort_function((Function(TableEntry<<String,String>>,TableEntry<<String,String>>)->Logical))
  0, // Table<<String,String>>.sort((Function(TableEntry<<String,String>>,TableEntry<<String,String>>)->Logical))
  0, // Table<<String,String>>.sorted((Function(TableEntry<<String,String>>,TableEntry<<String,String>>)->Logical))
  0, // Table<<String,String>>.values(String[])
  0, // Table<<String,String>>.reader()
  0, // Table<<String,String>>.key_reader()
  0, // Table<<String,String>>.value_reader()
  (void*) (ROGUEM4) RogueTable_String_String____adjust_entry_order__TableEntry_String_String_,
  (void*) (ROGUEM4) RogueTable_String_String____place_entry_in_order__TableEntry_String_String_,
  (void*) (ROGUEM4) RogueTable_String_String____unlink__TableEntry_String_String_,
  (void*) (ROGUEM0) RogueTable_String_String____grow,
  (void*) (ROGUEM1) RogueStringTable_String___init_object, // StringTable<<String>>
  (void*) (ROGUEM1) RogueTable_String_String___init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueTable_String_String___to_String,
  0, // Table<<String,String>>.to_Value()
  0, // Object.to_ValueList()
  0, // Table<<String,String>>.to_ValueTable()
  0, // Object.type_info()
  0, // Table<<String,String>>.unpack(Value)
  (void*) (ROGUEM1) RogueStringTable_String___type_name,
  (void*) (ROGUEM8) RogueTable_String_String___init__Int32,
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
  (void*) (ROGUEM3) RogueTable_String_String___find__String,
  0, // Table<<String,String>>.first()
  0, // Table<<String,String>>.first((Function(String)->Logical))
  (void*) (ROGUEM3) RogueTable_String_String___get__String,
  0, // Table<<String,String>>.get(String,String)
  0, // Table<<String,String>>.get((Function(String)->Logical))
  0, // Table<<String,String>>.keys(String[])
  0, // Table<<String,String>>.locate((Function(String)->Logical))
  (void*) (ROGUEM3) RogueTable_String_String___print_to__StringBuilder,
  0, // Table<<String,String>>.random()
  0, // Table<<String,String>>.remove(String)
  0, // Table<<String,String>>.remove((Function(String)->Logical))
  0, // Table<<String,String>>.remove(TableEntry<<String,String>>)
  (void*) (ROGUEM5) RogueTable_String_String___set__String_String,
  0, // Table<<String,String>>.set_sort_function((Function(TableEntry<<String,String>>,TableEntry<<String,String>>)->Logical))
  0, // Table<<String,String>>.sort((Function(TableEntry<<String,String>>,TableEntry<<String,String>>)->Logical))
  0, // Table<<String,String>>.sorted((Function(TableEntry<<String,String>>,TableEntry<<String,String>>)->Logical))
  0, // Table<<String,String>>.values(String[])
  0, // Table<<String,String>>.reader()
  0, // Table<<String,String>>.key_reader()
  0, // Table<<String,String>>.value_reader()
  (void*) (ROGUEM4) RogueTable_String_String____adjust_entry_order__TableEntry_String_String_,
  (void*) (ROGUEM4) RogueTable_String_String____place_entry_in_order__TableEntry_String_String_,
  (void*) (ROGUEM4) RogueTable_String_String____unlink__TableEntry_String_String_,
  (void*) (ROGUEM0) RogueTable_String_String____grow,
  (void*) (ROGUEM1) RogueTable_String_String___init_object, // Table<<String,String>>
  (void*) (ROGUEM1) RogueTable_String_String___init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueTable_String_String___to_String,
  0, // Table<<String,String>>.to_Value()
  0, // Object.to_ValueList()
  0, // Table<<String,String>>.to_ValueTable()
  0, // Object.type_info()
  0, // Table<<String,String>>.unpack(Value)
  (void*) (ROGUEM1) RogueTable_String_String___type_name,
  (void*) (ROGUEM8) RogueTable_String_String___init__Int32,
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
  (void*) (ROGUEM3) RogueTable_String_String___find__String,
  0, // Table<<String,String>>.first()
  0, // Table<<String,String>>.first((Function(String)->Logical))
  (void*) (ROGUEM3) RogueTable_String_String___get__String,
  0, // Table<<String,String>>.get(String,String)
  0, // Table<<String,String>>.get((Function(String)->Logical))
  0, // Table<<String,String>>.keys(String[])
  0, // Table<<String,String>>.locate((Function(String)->Logical))
  (void*) (ROGUEM3) RogueTable_String_String___print_to__StringBuilder,
  0, // Table<<String,String>>.random()
  0, // Table<<String,String>>.remove(String)
  0, // Table<<String,String>>.remove((Function(String)->Logical))
  0, // Table<<String,String>>.remove(TableEntry<<String,String>>)
  (void*) (ROGUEM5) RogueTable_String_String___set__String_String,
  0, // Table<<String,String>>.set_sort_function((Function(TableEntry<<String,String>>,TableEntry<<String,String>>)->Logical))
  0, // Table<<String,String>>.sort((Function(TableEntry<<String,String>>,TableEntry<<String,String>>)->Logical))
  0, // Table<<String,String>>.sorted((Function(TableEntry<<String,String>>,TableEntry<<String,String>>)->Logical))
  0, // Table<<String,String>>.values(String[])
  0, // Table<<String,String>>.reader()
  0, // Table<<String,String>>.key_reader()
  0, // Table<<String,String>>.value_reader()
  (void*) (ROGUEM4) RogueTable_String_String____adjust_entry_order__TableEntry_String_String_,
  (void*) (ROGUEM4) RogueTable_String_String____place_entry_in_order__TableEntry_String_String_,
  (void*) (ROGUEM4) RogueTable_String_String____unlink__TableEntry_String_String_,
  (void*) (ROGUEM0) RogueTable_String_String____grow,
  (void*) (ROGUEM0) RogueObject__init_object, // Array<<TableEntry<<String,String>>>>
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueArray_TableEntry_String_String____type_name,
  (void*) (ROGUEM29) RogueArray__zero__Int32_Int32,
  (void*) (ROGUEM1) RogueTableEntry_String_String___init_object, // TableEntry<<String,String>>
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueTableEntry_String_String___to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueTableEntry_String_String___type_name,
  (void*) (ROGUEM30) RogueTableEntry_String_String___init__String_String_Int32,
  (void*) (ROGUEM1) Rogue_Function_TableEntry_String_String__TableEntry_String_String__RETURNSLogical___init_object, // (Function(TableEntry<<String,String>>,TableEntry<<String,String>>)->Logical)
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) Rogue_Function_TableEntry_String_String__TableEntry_String_String__RETURNSLogical___type_name,
  (void*) (ROGUEM42) Rogue_Function_TableEntry_String_String__TableEntry_String_String__RETURNSLogical___call__TableEntry_String_String__TableEntry_String_String_,
  (void*) (ROGUEM1) RogueReader_Character___close, // Reader<<Character>>
  0, // Reader<<Character>>.has_another()
  0, // Reader<<Character>>.peek()
  0, // Reader<<Character>>.on_use()
  0, // Reader<<Character>>.on_end_use(Exception)
  0, // Reader<<Character>>.position()
  0, // Reader<<Character>>.read()
  0, // Reader<<String>>.close() // Reader<<String>>
  (void*) (ROGUEM1) RogueConsole__init_object, // Console
  (void*) (ROGUEM1) RogueConsole__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueConsole__type_name,
  (void*) (ROGUEM1) RogueConsole__close,
  (void*) (ROGUEM9) RogueConsole__has_another,
  (void*) (ROGUEM12) RogueConsole__read,
  (void*) (ROGUEM1) RogueConsole__flush,
  (void*) (ROGUEM3) RogueConsole__print__String,
  (void*) (ROGUEM1) RogueConsole__println,
  (void*) (ROGUEM3) RogueConsole__println__String,
  (void*) (ROGUEM3) RogueConsole__write__StringBuilder,
  (void*) (ROGUEM0) RogueConsole__fill_input_queue,
  (void*) (ROGUEM0) RogueConsole__reset_input_mode,
  (void*) (ROGUEM7) RogueConsole__width,
  (void*) (ROGUEM1) RoguePrintWriter_output_buffer___close, // PrintWriter<<output_buffer>>
  (void*) (ROGUEM1) RoguePrintWriter_output_buffer___flush,
  0, // PrintWriter<<output_buffer>>.on_use()
  0, // PrintWriter<<output_buffer>>.on_end_use(Exception)
  0, // PrintWriter<<output_buffer>>.print(Byte)
  0, // PrintWriter<<output_buffer>>.print(Character)
  0, // PrintWriter<<output_buffer>>.print(Int32)
  0, // PrintWriter<<output_buffer>>.print(Logical)
  0, // PrintWriter<<output_buffer>>.print(Int64)
  0, // PrintWriter<<output_buffer>>.print(Object)
  0, // PrintWriter<<output_buffer>>.print(Real64)
  0, // PrintWriter<<output_buffer>>.print(Real64,Int32)
  0, // PrintWriter<<output_buffer>>.print(Real32)
  0, // PrintWriter<<output_buffer>>.print(String)
  0, // PrintWriter<<output_buffer>>.println()
  0, // PrintWriter<<output_buffer>>.println(Byte)
  0, // PrintWriter<<output_buffer>>.println(Character)
  0, // PrintWriter<<output_buffer>>.println(Int32)
  0, // PrintWriter<<output_buffer>>.println(Logical)
  0, // PrintWriter<<output_buffer>>.println(Int64)
  0, // PrintWriter<<output_buffer>>.println(Real64)
  0, // PrintWriter<<output_buffer>>.println(Real64,Int32)
  0, // PrintWriter<<output_buffer>>.println(Real32)
  0, // PrintWriter<<output_buffer>>.println(Object)
  (void*) (ROGUEM3) RoguePrintWriter_output_buffer___println__String,
  0, // PrintWriter<<output_buffer>>.write(StringBuilder)
  (void*) (ROGUEM1) RogueConsoleErrorPrinter__init_object, // ConsoleErrorPrinter
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueConsoleErrorPrinter__type_name,
  (void*) (ROGUEM1) RogueConsoleErrorPrinter__close,
  (void*) (ROGUEM1) RogueConsoleErrorPrinter__flush,
  (void*) (ROGUEM3) RogueConsoleErrorPrinter__print__String,
  (void*) (ROGUEM1) RogueConsoleErrorPrinter__println,
  (void*) (ROGUEM3) RogueConsoleErrorPrinter__println__String,
  (void*) (ROGUEM3) RogueConsoleErrorPrinter__write__StringBuilder,
  (void*) (ROGUEM1) RoguePrimitiveWorkBuffer__init_object, // PrimitiveWorkBuffer
  (void*) (ROGUEM1) RogueStringBuilder__init,
  0, // StringBuilder.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueStringBuilder__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RoguePrimitiveWorkBuffer__type_name,
  (void*) (ROGUEM8) RogueStringBuilder__init__Int32,
  0, // StringBuilder.init(String)
  (void*) (ROGUEM1) RogueStringBuilder__clear,
  0, // StringBuilder.consolidated()
  0, // StringBuilder.contains(Character)
  0, // StringBuilder.discard_from(Int32)
  (void*) (ROGUEM43) RogueStringBuilder__get__Int32,
  0, // StringBuilder.insert(Character)
  0, // StringBuilder.insert_spaces(Int32,Int32)
  (void*) (ROGUEM44) RogueStringBuilder__locate__Character,
  0, // StringBuilder.locate_last(Character)
  (void*) (ROGUEM9) RogueStringBuilder__needs_indent,
  0, // StringBuilder.operator==(String)
  0, // StringBuilder.operator==(StringBuilder)
  0, // StringBuilder.print(Byte)
  (void*) (ROGUEM45) RogueStringBuilder__print__Character_Logical,
  (void*) (ROGUEM8) RogueStringBuilder__print__Int32,
  (void*) (ROGUEM38) RogueStringBuilder__print__Logical,
  (void*) (ROGUEM46) RogueStringBuilder__print__Int64,
  (void*) (ROGUEM3) RogueStringBuilder__print__Object,
  (void*) (ROGUEM39) RogueStringBuilder__print__Real64,
  (void*) (ROGUEM47) RogueStringBuilder__print__Real64_Int32,
  (void*) (ROGUEM3) RogueStringBuilder__print__String,
  (void*) (ROGUEM0) RogueStringBuilder__print_indent,
  (void*) (ROGUEM47) RogueStringBuilder__print_to_work_bytes__Real64_Int32,
  (void*) (ROGUEM0) RogueStringBuilder__print_work_bytes,
  (void*) (ROGUEM1) RogueStringBuilder__println,
  0, // StringBuilder.println(Byte)
  0, // StringBuilder.println(Character)
  0, // StringBuilder.println(Int32)
  0, // StringBuilder.println(Logical)
  0, // StringBuilder.println(Int64)
  0, // StringBuilder.println(Real64)
  0, // StringBuilder.println(Real64,Int32)
  0, // StringBuilder.println(Object)
  (void*) (ROGUEM3) RogueStringBuilder__println__String,
  0, // StringBuilder.remove_at(Int32)
  0, // StringBuilder.remove_last()
  (void*) (ROGUEM8) RogueStringBuilder__reserve__Int32,
  (void*) (ROGUEM0) RogueStringBuilder__round_off_work_bytes,
  (void*) (ROGUEM13) RogueStringBuilder__scan_work_bytes,
  (void*) (ROGUEM1) RogueMath__init_object, // Math
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueMath__type_name,
  (void*) (ROGUEM1) RogueBoxed__init_object, // Boxed
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Boxed.unpack(Value)
  (void*) (ROGUEM1) RogueBoxed__type_name,
  (void*) (ROGUEM1) RogueFunction_221__init_object, // Function_221
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
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
  (void*) (ROGUEM2) RogueObject__object_id,
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
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueException__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueError__type_name,
  (void*) (ROGUEM3) RogueException__init__String,
  (void*) (ROGUEM0) RogueException__display,
  (void*) (ROGUEM1) RogueException__format,
  (void*) (ROGUEM1) RogueError___throw,
  (void*) (ROGUEM1) RogueFile__init_object, // File
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueFile__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueFile__type_name,
  (void*) (ROGUEM3) RogueFile__init__String,
  (void*) (ROGUEM1) RogueFile__filename,
  (void*) (ROGUEM1) RogueLineReader__init_object, // LineReader
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueLineReader__type_name,
  (void*) (ROGUEM1) RogueLineReader__close,
  (void*) (ROGUEM9) RogueLineReader__has_another,
  (void*) (ROGUEM1) RogueLineReader__read,
  (void*) (ROGUEM3) RogueLineReader__init__Reader_Character_,
  (void*) (ROGUEM3) RogueLineReader__init__Reader_Byte_,
  (void*) (ROGUEM3) RogueLineReader__init__File,
  (void*) (ROGUEM1) RogueLineReader__prepare_next,
  (void*) (ROGUEM1) RogueReader_Byte___close, // Reader<<Byte>>
  0, // Reader<<Byte>>.has_another()
  0, // Reader<<Byte>>.peek()
  0, // Reader<<Byte>>.on_use()
  0, // Reader<<Byte>>.on_end_use(Exception)
  0, // Reader<<Byte>>.position()
  0, // Reader<<Byte>>.read()
  (void*) (ROGUEM1) RogueFileWriter__init_object, // FileWriter
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueFileWriter__type_name,
  (void*) (ROGUEM1) RogueFileWriter__close,
  (void*) (ROGUEM1) RogueFileWriter__flush,
  (void*) (ROGUEM49) RogueFileWriter__write__Byte,
  (void*) (ROGUEM3) RogueFileWriter__write__Byte_List,
  (void*) (ROGUEM78) RogueFileWriter__init__String_Logical,
  (void*) (ROGUEM0) RogueFileWriter__on_cleanup,
  (void*) (ROGUEM79) RogueFileWriter__open__String_Logical,
  (void*) (ROGUEM3) RogueFileWriter__write__String,
  (void*) (ROGUEM1) RogueWriter_Byte___close, // Writer<<Byte>>
  (void*) (ROGUEM1) RogueWriter_Byte___flush,
  0, // Writer<<Byte>>.on_use()
  0, // Writer<<Byte>>.on_end_use(Exception)
  0, // Writer<<Byte>>.position()
  0, // Writer<<Byte>>.reset()
  0, // Writer<<Byte>>.seek(Int32)
  0, // Writer<<Byte>>.seek_end()
  0, // Writer<<Byte>>.skip(Int32)
  0, // Writer<<Byte>>.write(Byte)
  (void*) (ROGUEM3) RogueWriter_Byte___write__Byte_List,
  (void*) (ROGUEM1) RogueScanner__init_object, // Scanner
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueScanner__type_name,
  (void*) (ROGUEM1) RogueScanner__close,
  (void*) (ROGUEM9) RogueScanner__has_another,
  (void*) (ROGUEM12) RogueScanner__peek,
  (void*) (ROGUEM12) RogueScanner__read,
  (void*) (ROGUEM80) RogueScanner__init__String_Int32_Logical,
  (void*) (ROGUEM32) RogueScanner__consume__Character,
  (void*) (ROGUEM9) RogueScanner__consume_eols,
  (void*) (ROGUEM9) RogueScanner__consume_spaces,
  (void*) (ROGUEM1) RogueScanner__convert_crlf_to_newline,
  (void*) (ROGUEM1) RogueJSONParser__init_object, // JSONParser
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueJSONParser__type_name,
  (void*) (ROGUEM3) RogueJSONParser__init__String,
  (void*) (ROGUEM3) RogueJSONParser__init__Scanner,
  (void*) (ROGUEM32) RogueJSONParser__consume__Character,
  (void*) (ROGUEM9) RogueJSONParser__has_another,
  (void*) (ROGUEM32) RogueJSONParser__next_is__Character,
  (void*) (ROGUEM1) RogueJSONParser__parse_value,
  (void*) (ROGUEM81) RogueJSONParser__parse_table__Character_Character,
  (void*) (ROGUEM81) RogueJSONParser__parse_list__Character_Character,
  (void*) (ROGUEM1) RogueJSONParser__parse_string,
  (void*) (ROGUEM12) RogueJSONParser__parse_hex_quad,
  (void*) (ROGUEM1) RogueJSONParser__parse_identifier,
  (void*) (ROGUEM12) RogueJSONParser__peek,
  (void*) (ROGUEM9) RogueJSONParser__next_is_identifier,
  (void*) (ROGUEM1) RogueJSONParser__parse_number,
  (void*) (ROGUEM12) RogueJSONParser__read,
  (void*) (ROGUEM0) RogueJSONParser__consume_spaces,
  (void*) (ROGUEM0) RogueJSONParser__consume_spaces_and_eols,
  (void*) (ROGUEM1) RogueJSON__init_object, // JSON
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
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
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) Rogue_Function_String_RETURNSString___type_name,
  (void*) (ROGUEM3) Rogue_Function_String_RETURNSString___call__String,
  (void*) (ROGUEM1) RogueFunction_281__init_object, // Function_281
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueFunction_281__type_name,
  (void*) (ROGUEM3) RogueFunction_281__call__String,
  (void*) (ROGUEM1) RogueRuntime__init_object, // Runtime
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
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
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueWeakReference__type_name,
  (void*) (ROGUEM0) RogueWeakReference__on_cleanup,
  (void*) (ROGUEM1) RoguePrintWriterAdapter__init_object, // PrintWriterAdapter
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RoguePrintWriterAdapter__type_name,
  (void*) (ROGUEM1) RoguePrintWriterAdapter__close,
  (void*) (ROGUEM1) RoguePrintWriterAdapter__flush,
  (void*) (ROGUEM3) RoguePrintWriterAdapter__print__String,
  (void*) (ROGUEM1) RoguePrintWriterAdapter__println,
  (void*) (ROGUEM3) RoguePrintWriterAdapter__println__String,
  (void*) (ROGUEM3) RoguePrintWriterAdapter__write__StringBuilder,
  (void*) (ROGUEM3) RoguePrintWriterAdapter__init__Writer_Byte_,
  (void*) (ROGUEM1) RoguePrintWriter_buffer___close, // PrintWriter<<buffer>>
  (void*) (ROGUEM1) RoguePrintWriter_buffer___flush,
  0, // PrintWriter<<buffer>>.on_use()
  0, // PrintWriter<<buffer>>.on_end_use(Exception)
  0, // PrintWriter<<buffer>>.print(Byte)
  0, // PrintWriter<<buffer>>.print(Character)
  0, // PrintWriter<<buffer>>.print(Int32)
  0, // PrintWriter<<buffer>>.print(Logical)
  0, // PrintWriter<<buffer>>.print(Int64)
  0, // PrintWriter<<buffer>>.print(Object)
  0, // PrintWriter<<buffer>>.print(Real64)
  0, // PrintWriter<<buffer>>.print(Real64,Int32)
  0, // PrintWriter<<buffer>>.print(Real32)
  0, // PrintWriter<<buffer>>.print(String)
  0, // PrintWriter<<buffer>>.println()
  0, // PrintWriter<<buffer>>.println(Byte)
  0, // PrintWriter<<buffer>>.println(Character)
  0, // PrintWriter<<buffer>>.println(Int32)
  0, // PrintWriter<<buffer>>.println(Logical)
  0, // PrintWriter<<buffer>>.println(Int64)
  0, // PrintWriter<<buffer>>.println(Real64)
  0, // PrintWriter<<buffer>>.println(Real64,Int32)
  0, // PrintWriter<<buffer>>.println(Real32)
  0, // PrintWriter<<buffer>>.println(Object)
  (void*) (ROGUEM3) RoguePrintWriter_buffer___println__String,
  0, // PrintWriter<<buffer>>.write(StringBuilder)
  (void*) (ROGUEM1) RogueLogicalValue__init_object, // LogicalValue
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
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
  (void*) (ROGUEM6) RogueValue__contains__String,
  (void*) (ROGUEM6) RogueValue__contains__Value,
  0, // Value.contains((Function(Value)->Logical))
  (void*) (ROGUEM7) RogueValue__count,
  0, // Value.count((Function(Value)->Logical))
  0, // Value.compressed()
  0, // Value.decompressed()
  0, // Value.decode_indexed(ValueIDLookupTable)
  0, // Value.encode_indexed(ValueIDTableBuilder)
  0, // Value.ensure_list(String)
  (void*) (ROGUEM3) RogueValue__ensure_table__String,
  0, // Value.first()
  0, // Value.first((Function(Value)->Logical))
  (void*) (ROGUEM8) RogueValue__get__Int32,
  (void*) (ROGUEM3) RogueValue__get__String,
  0, // Value.get((Function(Value)->Logical))
  0, // Value.insert(Value,Int32)
  0, // Value.insert(Value[],Int32)
  0, // Value.insert_all(Value,Int32)
  (void*) (ROGUEM9) RogueValue__is_collection,
  (void*) (ROGUEM9) RogueValue__is_complex,
  (void*) (ROGUEM9) RogueValue__is_int32,
  (void*) (ROGUEM9) RogueValue__is_int64,
  0, // Value.is_list()
  (void*) (ROGUEM9) RogueLogicalValue__is_logical,
  (void*) (ROGUEM9) RogueValue__is_null,
  (void*) (ROGUEM9) RogueValue__is_non_null,
  (void*) (ROGUEM9) RogueValue__is_number,
  (void*) (ROGUEM9) RogueValue__is_object,
  0, // Value.is_real64()
  (void*) (ROGUEM9) RogueValue__is_string,
  0, // Value.is_table()
  0, // Value.is_undefined()
  0, // Value.keys()
  0, // Value.last()
  0, // Value.last((Function(Value)->Logical))
  0, // Value.locate(Value)
  0, // Value.locate((Function(Value)->Logical))
  0, // Value.locate_last(Value)
  0, // Value.locate_last((Function(Value)->Logical))
  (void*) (ROGUEM6) RogueValue__operatorEQUALSEQUALS__Value,
  0, // Value.operator==(Byte)
  0, // Value.operator==(Character)
  0, // Value.operator==(Int32)
  0, // Value.operator==(Int64)
  0, // Value.operator==(Logical)
  0, // Value.operator==(Real64)
  0, // Value.operator==(Real32)
  (void*) (ROGUEM6) RogueValue__operatorEQUALSEQUALS__String,
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
  (void*) (ROGUEM3) RogueValue__remove__Value,
  0, // Value.remove(String)
  0, // Value.remove((Function(Value)->Logical))
  0, // Value.remove_at(Int32)
  0, // Value.remove_first()
  0, // Value.remove_last()
  0, // Value.reserve(Int32)
  0, // Value.rest(Value)
  (void*) (ROGUEM10) RogueValue__save__File_Logical_Logical,
  0, // Value.set(Int32,Value)
  (void*) (ROGUEM5) RogueValue__set__String_Value,
  0, // Value.sort((Function(Value,Value)->Logical))
  0, // Value.sorted((Function(Value,Value)->Logical))
  (void*) (ROGUEM11) RogueValue__to_Byte,
  (void*) (ROGUEM12) RogueValue__to_Character,
  (void*) (ROGUEM2) RogueLogicalValue__to_Int64,
  (void*) (ROGUEM7) RogueValue__to_Int32,
  (void*) (ROGUEM9) RogueLogicalValue__to_Logical,
  (void*) (ROGUEM13) RogueValue__to_Real64,
  (void*) (ROGUEM14) RogueValue__to_Real32,
  (void*) (ROGUEM1) RogueValue__to_Object,
  0, // Value.to_json(Int32)
  (void*) (ROGUEM15) RogueValue__to_json__Logical_Logical,
  (void*) (ROGUEM16) RogueValue__to_json__StringBuilder_Logical_Logical,
  (void*) (ROGUEM17) RogueLogicalValue__to_json__StringBuilder_Int32,
  0, // Value.values(Value)
  (void*) (ROGUEM38) RogueLogicalValue__init__Logical,
  (void*) (ROGUEM1) RogueInt32Value__init_object, // Int32Value
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Value.operator==(Object)
  (void*) (ROGUEM1) RogueInt32Value__to_String,
  0, // Value.to_Value()
  0, // Value.to_ValueList()
  0, // Value.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueInt32Value__type_name,
  0, // Value.add(Value)
  0, // Value.add(Value[])
  0, // Value.add_all(Value)
  0, // Value.clear()
  0, // Value.cloned()
  0, // Value.apply((Function(Value)->Value))
  (void*) (ROGUEM6) RogueValue__contains__String,
  (void*) (ROGUEM6) RogueValue__contains__Value,
  0, // Value.contains((Function(Value)->Logical))
  (void*) (ROGUEM7) RogueValue__count,
  0, // Value.count((Function(Value)->Logical))
  0, // Value.compressed()
  0, // Value.decompressed()
  0, // Value.decode_indexed(ValueIDLookupTable)
  0, // Value.encode_indexed(ValueIDTableBuilder)
  0, // Value.ensure_list(String)
  (void*) (ROGUEM3) RogueValue__ensure_table__String,
  0, // Value.first()
  0, // Value.first((Function(Value)->Logical))
  (void*) (ROGUEM8) RogueValue__get__Int32,
  (void*) (ROGUEM3) RogueValue__get__String,
  0, // Value.get((Function(Value)->Logical))
  0, // Value.insert(Value,Int32)
  0, // Value.insert(Value[],Int32)
  0, // Value.insert_all(Value,Int32)
  (void*) (ROGUEM9) RogueValue__is_collection,
  (void*) (ROGUEM9) RogueValue__is_complex,
  (void*) (ROGUEM9) RogueInt32Value__is_int32,
  (void*) (ROGUEM9) RogueValue__is_int64,
  0, // Value.is_list()
  (void*) (ROGUEM9) RogueValue__is_logical,
  (void*) (ROGUEM9) RogueValue__is_null,
  (void*) (ROGUEM9) RogueValue__is_non_null,
  (void*) (ROGUEM9) RogueInt32Value__is_number,
  (void*) (ROGUEM9) RogueValue__is_object,
  0, // Value.is_real64()
  (void*) (ROGUEM9) RogueValue__is_string,
  0, // Value.is_table()
  0, // Value.is_undefined()
  0, // Value.keys()
  0, // Value.last()
  0, // Value.last((Function(Value)->Logical))
  0, // Value.locate(Value)
  0, // Value.locate((Function(Value)->Logical))
  0, // Value.locate_last(Value)
  0, // Value.locate_last((Function(Value)->Logical))
  (void*) (ROGUEM6) RogueInt32Value__operatorEQUALSEQUALS__Value,
  0, // Value.operator==(Byte)
  0, // Value.operator==(Character)
  0, // Int32Value.operator==(Int32)
  0, // Int32Value.operator==(Int64)
  0, // Value.operator==(Logical)
  0, // Int32Value.operator==(Real64)
  0, // Value.operator==(Real32)
  (void*) (ROGUEM6) RogueValue__operatorEQUALSEQUALS__String,
  0, // Int32Value.operator<(Value)
  0, // Value.operator<(Byte)
  0, // Value.operator<(Character)
  0, // Int32Value.operator<(Int32)
  0, // Int32Value.operator<(Int64)
  0, // Value.operator<(Logical)
  0, // Int32Value.operator<(Real64)
  0, // Value.operator<(Real32)
  0, // Value.operator<(Object)
  0, // Value.operator<(String)
  0, // Int32Value.operator-()
  0, // Int32Value.operator+(Value)
  0, // Int32Value.operator-(Value)
  0, // Int32Value.operator*(Value)
  0, // Int32Value.operator/(Value)
  0, // Int32Value.operator%(Value)
  0, // Int32Value.operator+(Real64)
  0, // Int32Value.operator-(Real64)
  0, // Int32Value.operator*(Real64)
  0, // Int32Value.operator/(Real64)
  0, // Int32Value.operator%(Real64)
  0, // Int32Value.operator+(String)
  0, // Int32Value.operator*(String)
  (void*) (ROGUEM3) RogueValue__remove__Value,
  0, // Value.remove(String)
  0, // Value.remove((Function(Value)->Logical))
  0, // Value.remove_at(Int32)
  0, // Value.remove_first()
  0, // Value.remove_last()
  0, // Value.reserve(Int32)
  0, // Value.rest(Value)
  (void*) (ROGUEM10) RogueValue__save__File_Logical_Logical,
  0, // Value.set(Int32,Value)
  (void*) (ROGUEM5) RogueValue__set__String_Value,
  0, // Value.sort((Function(Value,Value)->Logical))
  0, // Value.sorted((Function(Value,Value)->Logical))
  (void*) (ROGUEM11) RogueValue__to_Byte,
  (void*) (ROGUEM12) RogueValue__to_Character,
  (void*) (ROGUEM2) RogueInt32Value__to_Int64,
  (void*) (ROGUEM7) RogueInt32Value__to_Int32,
  (void*) (ROGUEM9) RogueValue__to_Logical,
  (void*) (ROGUEM13) RogueInt32Value__to_Real64,
  (void*) (ROGUEM14) RogueValue__to_Real32,
  (void*) (ROGUEM1) RogueValue__to_Object,
  0, // Value.to_json(Int32)
  (void*) (ROGUEM15) RogueValue__to_json__Logical_Logical,
  (void*) (ROGUEM16) RogueValue__to_json__StringBuilder_Logical_Logical,
  (void*) (ROGUEM17) RogueInt32Value__to_json__StringBuilder_Int32,
  0, // Value.values(Value)
  (void*) (ROGUEM8) RogueInt32Value__init__Int32,
  (void*) (ROGUEM1) RogueInt64Value__init_object, // Int64Value
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Value.operator==(Object)
  (void*) (ROGUEM1) RogueInt64Value__to_String,
  0, // Value.to_Value()
  0, // Value.to_ValueList()
  0, // Value.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueInt64Value__type_name,
  0, // Value.add(Value)
  0, // Value.add(Value[])
  0, // Value.add_all(Value)
  0, // Value.clear()
  0, // Value.cloned()
  0, // Value.apply((Function(Value)->Value))
  (void*) (ROGUEM6) RogueValue__contains__String,
  (void*) (ROGUEM6) RogueValue__contains__Value,
  0, // Value.contains((Function(Value)->Logical))
  (void*) (ROGUEM7) RogueValue__count,
  0, // Value.count((Function(Value)->Logical))
  0, // Value.compressed()
  0, // Value.decompressed()
  0, // Value.decode_indexed(ValueIDLookupTable)
  0, // Value.encode_indexed(ValueIDTableBuilder)
  0, // Value.ensure_list(String)
  (void*) (ROGUEM3) RogueValue__ensure_table__String,
  0, // Value.first()
  0, // Value.first((Function(Value)->Logical))
  (void*) (ROGUEM8) RogueValue__get__Int32,
  (void*) (ROGUEM3) RogueValue__get__String,
  0, // Value.get((Function(Value)->Logical))
  0, // Value.insert(Value,Int32)
  0, // Value.insert(Value[],Int32)
  0, // Value.insert_all(Value,Int32)
  (void*) (ROGUEM9) RogueValue__is_collection,
  (void*) (ROGUEM9) RogueValue__is_complex,
  (void*) (ROGUEM9) RogueValue__is_int32,
  (void*) (ROGUEM9) RogueInt64Value__is_int64,
  0, // Value.is_list()
  (void*) (ROGUEM9) RogueValue__is_logical,
  (void*) (ROGUEM9) RogueValue__is_null,
  (void*) (ROGUEM9) RogueValue__is_non_null,
  (void*) (ROGUEM9) RogueInt64Value__is_number,
  (void*) (ROGUEM9) RogueValue__is_object,
  0, // Value.is_real64()
  (void*) (ROGUEM9) RogueValue__is_string,
  0, // Value.is_table()
  0, // Value.is_undefined()
  0, // Value.keys()
  0, // Value.last()
  0, // Value.last((Function(Value)->Logical))
  0, // Value.locate(Value)
  0, // Value.locate((Function(Value)->Logical))
  0, // Value.locate_last(Value)
  0, // Value.locate_last((Function(Value)->Logical))
  (void*) (ROGUEM6) RogueInt64Value__operatorEQUALSEQUALS__Value,
  0, // Value.operator==(Byte)
  0, // Value.operator==(Character)
  0, // Int64Value.operator==(Int32)
  0, // Int64Value.operator==(Int64)
  0, // Value.operator==(Logical)
  0, // Int64Value.operator==(Real64)
  0, // Value.operator==(Real32)
  (void*) (ROGUEM6) RogueValue__operatorEQUALSEQUALS__String,
  0, // Int64Value.operator<(Value)
  0, // Value.operator<(Byte)
  0, // Value.operator<(Character)
  0, // Int64Value.operator<(Int32)
  0, // Int64Value.operator<(Int64)
  0, // Value.operator<(Logical)
  0, // Int64Value.operator<(Real64)
  0, // Value.operator<(Real32)
  0, // Value.operator<(Object)
  0, // Value.operator<(String)
  0, // Int64Value.operator-()
  0, // Int64Value.operator+(Value)
  0, // Int64Value.operator-(Value)
  0, // Int64Value.operator*(Value)
  0, // Int64Value.operator/(Value)
  0, // Int64Value.operator%(Value)
  0, // Int64Value.operator+(Real64)
  0, // Int64Value.operator-(Real64)
  0, // Int64Value.operator*(Real64)
  0, // Int64Value.operator/(Real64)
  0, // Int64Value.operator%(Real64)
  0, // Int64Value.operator+(String)
  0, // Int64Value.operator*(String)
  (void*) (ROGUEM3) RogueValue__remove__Value,
  0, // Value.remove(String)
  0, // Value.remove((Function(Value)->Logical))
  0, // Value.remove_at(Int32)
  0, // Value.remove_first()
  0, // Value.remove_last()
  0, // Value.reserve(Int32)
  0, // Value.rest(Value)
  (void*) (ROGUEM10) RogueValue__save__File_Logical_Logical,
  0, // Value.set(Int32,Value)
  (void*) (ROGUEM5) RogueValue__set__String_Value,
  0, // Value.sort((Function(Value,Value)->Logical))
  0, // Value.sorted((Function(Value,Value)->Logical))
  (void*) (ROGUEM11) RogueValue__to_Byte,
  (void*) (ROGUEM12) RogueValue__to_Character,
  (void*) (ROGUEM2) RogueInt64Value__to_Int64,
  (void*) (ROGUEM7) RogueInt64Value__to_Int32,
  (void*) (ROGUEM9) RogueValue__to_Logical,
  (void*) (ROGUEM13) RogueInt64Value__to_Real64,
  (void*) (ROGUEM14) RogueValue__to_Real32,
  (void*) (ROGUEM1) RogueValue__to_Object,
  0, // Value.to_json(Int32)
  (void*) (ROGUEM15) RogueValue__to_json__Logical_Logical,
  (void*) (ROGUEM16) RogueValue__to_json__StringBuilder_Logical_Logical,
  (void*) (ROGUEM17) RogueInt64Value__to_json__StringBuilder_Int32,
  0, // Value.values(Value)
  (void*) (ROGUEM46) RogueInt64Value__init__Int64,
  (void*) (ROGUEM1) RogueReal64Value__init_object, // Real64Value
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
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
  (void*) (ROGUEM6) RogueValue__contains__String,
  (void*) (ROGUEM6) RogueValue__contains__Value,
  0, // Value.contains((Function(Value)->Logical))
  (void*) (ROGUEM7) RogueValue__count,
  0, // Value.count((Function(Value)->Logical))
  0, // Value.compressed()
  0, // Value.decompressed()
  0, // Value.decode_indexed(ValueIDLookupTable)
  0, // Value.encode_indexed(ValueIDTableBuilder)
  0, // Value.ensure_list(String)
  (void*) (ROGUEM3) RogueValue__ensure_table__String,
  0, // Value.first()
  0, // Value.first((Function(Value)->Logical))
  (void*) (ROGUEM8) RogueValue__get__Int32,
  (void*) (ROGUEM3) RogueValue__get__String,
  0, // Value.get((Function(Value)->Logical))
  0, // Value.insert(Value,Int32)
  0, // Value.insert(Value[],Int32)
  0, // Value.insert_all(Value,Int32)
  (void*) (ROGUEM9) RogueValue__is_collection,
  (void*) (ROGUEM9) RogueValue__is_complex,
  (void*) (ROGUEM9) RogueValue__is_int32,
  (void*) (ROGUEM9) RogueValue__is_int64,
  0, // Value.is_list()
  (void*) (ROGUEM9) RogueValue__is_logical,
  (void*) (ROGUEM9) RogueValue__is_null,
  (void*) (ROGUEM9) RogueValue__is_non_null,
  (void*) (ROGUEM9) RogueReal64Value__is_number,
  (void*) (ROGUEM9) RogueValue__is_object,
  0, // Real64Value.is_real64()
  (void*) (ROGUEM9) RogueValue__is_string,
  0, // Value.is_table()
  0, // Value.is_undefined()
  0, // Value.keys()
  0, // Value.last()
  0, // Value.last((Function(Value)->Logical))
  0, // Value.locate(Value)
  0, // Value.locate((Function(Value)->Logical))
  0, // Value.locate_last(Value)
  0, // Value.locate_last((Function(Value)->Logical))
  (void*) (ROGUEM6) RogueReal64Value__operatorEQUALSEQUALS__Value,
  0, // Value.operator==(Byte)
  0, // Value.operator==(Character)
  0, // Value.operator==(Int32)
  0, // Value.operator==(Int64)
  0, // Value.operator==(Logical)
  0, // Value.operator==(Real64)
  0, // Value.operator==(Real32)
  (void*) (ROGUEM6) RogueValue__operatorEQUALSEQUALS__String,
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
  (void*) (ROGUEM3) RogueValue__remove__Value,
  0, // Value.remove(String)
  0, // Value.remove((Function(Value)->Logical))
  0, // Value.remove_at(Int32)
  0, // Value.remove_first()
  0, // Value.remove_last()
  0, // Value.reserve(Int32)
  0, // Value.rest(Value)
  (void*) (ROGUEM10) RogueValue__save__File_Logical_Logical,
  0, // Value.set(Int32,Value)
  (void*) (ROGUEM5) RogueValue__set__String_Value,
  0, // Value.sort((Function(Value,Value)->Logical))
  0, // Value.sorted((Function(Value,Value)->Logical))
  (void*) (ROGUEM11) RogueValue__to_Byte,
  (void*) (ROGUEM12) RogueValue__to_Character,
  (void*) (ROGUEM2) RogueReal64Value__to_Int64,
  (void*) (ROGUEM7) RogueValue__to_Int32,
  (void*) (ROGUEM9) RogueValue__to_Logical,
  (void*) (ROGUEM13) RogueReal64Value__to_Real64,
  (void*) (ROGUEM14) RogueValue__to_Real32,
  (void*) (ROGUEM1) RogueValue__to_Object,
  0, // Value.to_json(Int32)
  (void*) (ROGUEM15) RogueValue__to_json__Logical_Logical,
  (void*) (ROGUEM16) RogueValue__to_json__StringBuilder_Logical_Logical,
  (void*) (ROGUEM17) RogueReal64Value__to_json__StringBuilder_Int32,
  0, // Value.values(Value)
  (void*) (ROGUEM39) RogueReal64Value__init__Real64,
  (void*) (ROGUEM1) RogueNullValue__init_object, // NullValue
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
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
  (void*) (ROGUEM6) RogueValue__contains__String,
  (void*) (ROGUEM6) RogueValue__contains__Value,
  0, // Value.contains((Function(Value)->Logical))
  (void*) (ROGUEM7) RogueValue__count,
  0, // Value.count((Function(Value)->Logical))
  0, // Value.compressed()
  0, // Value.decompressed()
  0, // Value.decode_indexed(ValueIDLookupTable)
  0, // Value.encode_indexed(ValueIDTableBuilder)
  0, // Value.ensure_list(String)
  (void*) (ROGUEM3) RogueValue__ensure_table__String,
  0, // Value.first()
  0, // Value.first((Function(Value)->Logical))
  (void*) (ROGUEM8) RogueValue__get__Int32,
  (void*) (ROGUEM3) RogueValue__get__String,
  0, // Value.get((Function(Value)->Logical))
  0, // Value.insert(Value,Int32)
  0, // Value.insert(Value[],Int32)
  0, // Value.insert_all(Value,Int32)
  (void*) (ROGUEM9) RogueValue__is_collection,
  (void*) (ROGUEM9) RogueValue__is_complex,
  (void*) (ROGUEM9) RogueValue__is_int32,
  (void*) (ROGUEM9) RogueValue__is_int64,
  0, // Value.is_list()
  (void*) (ROGUEM9) RogueValue__is_logical,
  (void*) (ROGUEM9) RogueNullValue__is_null,
  (void*) (ROGUEM9) RogueNullValue__is_non_null,
  (void*) (ROGUEM9) RogueValue__is_number,
  (void*) (ROGUEM9) RogueValue__is_object,
  0, // Value.is_real64()
  (void*) (ROGUEM9) RogueValue__is_string,
  0, // Value.is_table()
  0, // Value.is_undefined()
  0, // Value.keys()
  0, // Value.last()
  0, // Value.last((Function(Value)->Logical))
  0, // Value.locate(Value)
  0, // Value.locate((Function(Value)->Logical))
  0, // Value.locate_last(Value)
  0, // Value.locate_last((Function(Value)->Logical))
  (void*) (ROGUEM6) RogueNullValue__operatorEQUALSEQUALS__Value,
  0, // Value.operator==(Byte)
  0, // Value.operator==(Character)
  0, // Value.operator==(Int32)
  0, // Value.operator==(Int64)
  0, // Value.operator==(Logical)
  0, // Value.operator==(Real64)
  0, // Value.operator==(Real32)
  (void*) (ROGUEM6) RogueValue__operatorEQUALSEQUALS__String,
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
  (void*) (ROGUEM3) RogueValue__remove__Value,
  0, // Value.remove(String)
  0, // Value.remove((Function(Value)->Logical))
  0, // Value.remove_at(Int32)
  0, // Value.remove_first()
  0, // Value.remove_last()
  0, // Value.reserve(Int32)
  0, // Value.rest(Value)
  (void*) (ROGUEM10) RogueValue__save__File_Logical_Logical,
  0, // Value.set(Int32,Value)
  (void*) (ROGUEM5) RogueValue__set__String_Value,
  0, // Value.sort((Function(Value,Value)->Logical))
  0, // Value.sorted((Function(Value,Value)->Logical))
  (void*) (ROGUEM11) RogueValue__to_Byte,
  (void*) (ROGUEM12) RogueValue__to_Character,
  (void*) (ROGUEM2) RogueNullValue__to_Int64,
  (void*) (ROGUEM7) RogueValue__to_Int32,
  (void*) (ROGUEM9) RogueValue__to_Logical,
  (void*) (ROGUEM13) RogueValue__to_Real64,
  (void*) (ROGUEM14) RogueValue__to_Real32,
  (void*) (ROGUEM1) RogueValue__to_Object,
  0, // Value.to_json(Int32)
  (void*) (ROGUEM15) RogueValue__to_json__Logical_Logical,
  (void*) (ROGUEM16) RogueValue__to_json__StringBuilder_Logical_Logical,
  (void*) (ROGUEM17) RogueNullValue__to_json__StringBuilder_Int32,
  (void*) (ROGUEM1) RogueStringValue__init_object, // StringValue
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
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
  (void*) (ROGUEM6) RogueValue__contains__String,
  (void*) (ROGUEM6) RogueValue__contains__Value,
  0, // Value.contains((Function(Value)->Logical))
  (void*) (ROGUEM7) RogueStringValue__count,
  0, // Value.count((Function(Value)->Logical))
  0, // Value.compressed()
  0, // Value.decompressed()
  0, // StringValue.decode_indexed(ValueIDLookupTable)
  0, // StringValue.encode_indexed(ValueIDTableBuilder)
  0, // Value.ensure_list(String)
  (void*) (ROGUEM3) RogueValue__ensure_table__String,
  0, // Value.first()
  0, // Value.first((Function(Value)->Logical))
  (void*) (ROGUEM8) RogueStringValue__get__Int32,
  (void*) (ROGUEM3) RogueValue__get__String,
  0, // Value.get((Function(Value)->Logical))
  0, // Value.insert(Value,Int32)
  0, // Value.insert(Value[],Int32)
  0, // Value.insert_all(Value,Int32)
  (void*) (ROGUEM9) RogueValue__is_collection,
  (void*) (ROGUEM9) RogueValue__is_complex,
  (void*) (ROGUEM9) RogueValue__is_int32,
  (void*) (ROGUEM9) RogueValue__is_int64,
  0, // Value.is_list()
  (void*) (ROGUEM9) RogueValue__is_logical,
  (void*) (ROGUEM9) RogueValue__is_null,
  (void*) (ROGUEM9) RogueValue__is_non_null,
  (void*) (ROGUEM9) RogueValue__is_number,
  (void*) (ROGUEM9) RogueValue__is_object,
  0, // Value.is_real64()
  (void*) (ROGUEM9) RogueStringValue__is_string,
  0, // Value.is_table()
  0, // Value.is_undefined()
  0, // Value.keys()
  0, // Value.last()
  0, // Value.last((Function(Value)->Logical))
  0, // Value.locate(Value)
  0, // Value.locate((Function(Value)->Logical))
  0, // Value.locate_last(Value)
  0, // Value.locate_last((Function(Value)->Logical))
  (void*) (ROGUEM6) RogueStringValue__operatorEQUALSEQUALS__Value,
  0, // Value.operator==(Byte)
  0, // Value.operator==(Character)
  0, // Value.operator==(Int32)
  0, // Value.operator==(Int64)
  0, // Value.operator==(Logical)
  0, // Value.operator==(Real64)
  0, // Value.operator==(Real32)
  (void*) (ROGUEM6) RogueValue__operatorEQUALSEQUALS__String,
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
  (void*) (ROGUEM3) RogueValue__remove__Value,
  0, // Value.remove(String)
  0, // Value.remove((Function(Value)->Logical))
  0, // Value.remove_at(Int32)
  0, // Value.remove_first()
  0, // Value.remove_last()
  0, // Value.reserve(Int32)
  0, // Value.rest(Value)
  (void*) (ROGUEM10) RogueValue__save__File_Logical_Logical,
  0, // Value.set(Int32,Value)
  (void*) (ROGUEM5) RogueValue__set__String_Value,
  0, // Value.sort((Function(Value,Value)->Logical))
  0, // Value.sorted((Function(Value,Value)->Logical))
  (void*) (ROGUEM11) RogueValue__to_Byte,
  (void*) (ROGUEM12) RogueStringValue__to_Character,
  (void*) (ROGUEM2) RogueStringValue__to_Int64,
  (void*) (ROGUEM7) RogueValue__to_Int32,
  (void*) (ROGUEM9) RogueStringValue__to_Logical,
  (void*) (ROGUEM13) RogueStringValue__to_Real64,
  (void*) (ROGUEM14) RogueValue__to_Real32,
  (void*) (ROGUEM1) RogueStringValue__to_Object,
  0, // Value.to_json(Int32)
  (void*) (ROGUEM15) RogueValue__to_json__Logical_Logical,
  (void*) (ROGUEM16) RogueValue__to_json__StringBuilder_Logical_Logical,
  (void*) (ROGUEM17) RogueStringValue__to_json__StringBuilder_Int32,
  0, // Value.values(Value)
  (void*) (ROGUEM3) RogueStringValue__init__String,
  (void*) (ROGUEM1) RogueUndefinedValue__init_object, // UndefinedValue
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
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
  (void*) (ROGUEM6) RogueValue__contains__String,
  (void*) (ROGUEM6) RogueValue__contains__Value,
  0, // Value.contains((Function(Value)->Logical))
  (void*) (ROGUEM7) RogueValue__count,
  0, // Value.count((Function(Value)->Logical))
  0, // Value.compressed()
  0, // Value.decompressed()
  0, // Value.decode_indexed(ValueIDLookupTable)
  0, // Value.encode_indexed(ValueIDTableBuilder)
  0, // Value.ensure_list(String)
  (void*) (ROGUEM3) RogueValue__ensure_table__String,
  0, // Value.first()
  0, // Value.first((Function(Value)->Logical))
  (void*) (ROGUEM8) RogueValue__get__Int32,
  (void*) (ROGUEM3) RogueValue__get__String,
  0, // Value.get((Function(Value)->Logical))
  0, // Value.insert(Value,Int32)
  0, // Value.insert(Value[],Int32)
  0, // Value.insert_all(Value,Int32)
  (void*) (ROGUEM9) RogueValue__is_collection,
  (void*) (ROGUEM9) RogueValue__is_complex,
  (void*) (ROGUEM9) RogueValue__is_int32,
  (void*) (ROGUEM9) RogueValue__is_int64,
  0, // Value.is_list()
  (void*) (ROGUEM9) RogueValue__is_logical,
  (void*) (ROGUEM9) RogueNullValue__is_null,
  (void*) (ROGUEM9) RogueNullValue__is_non_null,
  (void*) (ROGUEM9) RogueValue__is_number,
  (void*) (ROGUEM9) RogueValue__is_object,
  0, // Value.is_real64()
  (void*) (ROGUEM9) RogueValue__is_string,
  0, // Value.is_table()
  0, // UndefinedValue.is_undefined()
  0, // Value.keys()
  0, // Value.last()
  0, // Value.last((Function(Value)->Logical))
  0, // Value.locate(Value)
  0, // Value.locate((Function(Value)->Logical))
  0, // Value.locate_last(Value)
  0, // Value.locate_last((Function(Value)->Logical))
  (void*) (ROGUEM6) RogueNullValue__operatorEQUALSEQUALS__Value,
  0, // Value.operator==(Byte)
  0, // Value.operator==(Character)
  0, // Value.operator==(Int32)
  0, // Value.operator==(Int64)
  0, // Value.operator==(Logical)
  0, // Value.operator==(Real64)
  0, // Value.operator==(Real32)
  (void*) (ROGUEM6) RogueValue__operatorEQUALSEQUALS__String,
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
  (void*) (ROGUEM3) RogueValue__remove__Value,
  0, // Value.remove(String)
  0, // Value.remove((Function(Value)->Logical))
  0, // Value.remove_at(Int32)
  0, // Value.remove_first()
  0, // Value.remove_last()
  0, // Value.reserve(Int32)
  0, // Value.rest(Value)
  (void*) (ROGUEM10) RogueValue__save__File_Logical_Logical,
  0, // Value.set(Int32,Value)
  (void*) (ROGUEM5) RogueValue__set__String_Value,
  0, // Value.sort((Function(Value,Value)->Logical))
  0, // Value.sorted((Function(Value,Value)->Logical))
  (void*) (ROGUEM11) RogueValue__to_Byte,
  (void*) (ROGUEM12) RogueValue__to_Character,
  (void*) (ROGUEM2) RogueNullValue__to_Int64,
  (void*) (ROGUEM7) RogueValue__to_Int32,
  (void*) (ROGUEM9) RogueValue__to_Logical,
  (void*) (ROGUEM13) RogueValue__to_Real64,
  (void*) (ROGUEM14) RogueValue__to_Real32,
  (void*) (ROGUEM1) RogueValue__to_Object,
  0, // Value.to_json(Int32)
  (void*) (ROGUEM15) RogueValue__to_json__Logical_Logical,
  (void*) (ROGUEM16) RogueValue__to_json__StringBuilder_Logical_Logical,
  (void*) (ROGUEM17) RogueNullValue__to_json__StringBuilder_Int32,
  (void*) (ROGUEM1) RogueOutOfBoundsError__init_object, // OutOfBoundsError
  (void*) (ROGUEM1) RogueException__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueException__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueOutOfBoundsError__type_name,
  (void*) (ROGUEM3) RogueOutOfBoundsError__init__String,
  (void*) (ROGUEM0) RogueException__display,
  (void*) (ROGUEM1) RogueException__format,
  (void*) (ROGUEM1) RogueOutOfBoundsError___throw,
  (void*) (ROGUEM34) RogueOutOfBoundsError__init__Int32_Int32,
  (void*) (ROGUEM1) RogueTableEntry_String_TypeInfo___init_object, // TableEntry<<String,TypeInfo>>
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueTableEntry_String_TypeInfo___to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueTableEntry_String_TypeInfo___type_name,
  (void*) (ROGUEM1) RogueRequirementError__init_object, // RequirementError
  (void*) (ROGUEM1) RogueException__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueException__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueRequirementError__type_name,
  (void*) (ROGUEM3) RogueRequirementError__init__String,
  (void*) (ROGUEM0) RogueException__display,
  (void*) (ROGUEM1) RogueException__format,
  (void*) (ROGUEM1) RogueRequirementError___throw,
  (void*) (ROGUEM1) RogueListRewriter_Character___init_object, // ListRewriter<<Character>>
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueListRewriter_Character___type_name,
  (void*) (ROGUEM3) RogueListRewriter_Character___init__Character_List,
  (void*) (ROGUEM9) RogueListRewriter_Character___has_another,
  (void*) (ROGUEM12) RogueListRewriter_Character___read,
  (void*) (ROGUEM31) RogueListRewriter_Character___write__Character,
  (void*) (ROGUEM1) RogueOptionalBoxed_Int32___init_object, // Boxed<<Int32?>>
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueOptionalBoxed_Int32___to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Boxed<<Int32?>>.to_ValueTable()
  0, // Object.type_info()
  0, // Boxed<<Int32?>>.unpack(Value)
  (void*) (ROGUEM1) RogueOptionalBoxed_Int32___type_name,
  0, // Boxed<<Int32?>>.address()
  0, // Boxed<<Int32?>>.inner_introspector()
  0, // Boxed<<Int32?>>.size()
  0, // Boxed<<Int32?>>.type()
  (void*) (ROGUEM82) RogueOptionalBoxed_Int32___init__OptionalInt32,
  (void*) (ROGUEM83) RogueOptionalOptionalBoxed_Int32___to_Int32,
  (void*) (ROGUEM1) RogueFunction_1182__init_object, // Function_1182
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueFunction_1182__type_name,
  (void*) (ROGUEM0) RogueFunction_1182__call,
  (void*) (ROGUEM3) RogueFunction_1182__init__Console,
  (void*) (ROGUEM1) RogueBoxed_SystemEnvironment___init_object, // Boxed<<SystemEnvironment>>
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueBoxed_SystemEnvironment___to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Boxed<<SystemEnvironment>>.to_ValueTable()
  0, // Object.type_info()
  0, // Boxed<<SystemEnvironment>>.unpack(Value)
  (void*) (ROGUEM1) RogueBoxed_SystemEnvironment___type_name,
  0, // Boxed<<SystemEnvironment>>.address()
  0, // Boxed<<SystemEnvironment>>.inner_introspector()
  0, // Boxed<<SystemEnvironment>>.size()
  0, // Boxed<<SystemEnvironment>>.type()
  (void*) (ROGUEM84) RogueBoxed_SystemEnvironment___init__SystemEnvironment,
  (void*) (ROGUEM85) RogueBoxed_SystemEnvironment___to_SystemEnvironment,
  (void*) (ROGUEM1) RogueIOError__init_object, // IOError
  (void*) (ROGUEM1) RogueException__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueException__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueIOError__type_name,
  (void*) (ROGUEM3) RogueException__init__String,
  (void*) (ROGUEM0) RogueException__display,
  (void*) (ROGUEM1) RogueException__format,
  (void*) (ROGUEM1) RogueIOError___throw,
  (void*) (ROGUEM1) RogueFileReader__init_object, // FileReader
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueFileReader__type_name,
  (void*) (ROGUEM1) RogueFileReader__close,
  (void*) (ROGUEM9) RogueFileReader__has_another,
  (void*) (ROGUEM11) RogueFileReader__peek,
  (void*) (ROGUEM11) RogueFileReader__read,
  (void*) (ROGUEM3) RogueFileReader__init__String,
  (void*) (ROGUEM0) RogueFileReader__on_cleanup,
  (void*) (ROGUEM6) RogueFileReader__open__String,
  (void*) (ROGUEM1) RogueUTF8Reader__init_object, // UTF8Reader
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueObject__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueUTF8Reader__type_name,
  (void*) (ROGUEM1) RogueUTF8Reader__close,
  (void*) (ROGUEM9) RogueUTF8Reader__has_another,
  (void*) (ROGUEM12) RogueUTF8Reader__peek,
  (void*) (ROGUEM12) RogueUTF8Reader__read,
  (void*) (ROGUEM3) RogueUTF8Reader__init__Reader_Byte_,
  (void*) (ROGUEM1) RogueJSONParseError__init_object, // JSONParseError
  (void*) (ROGUEM1) RogueException__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueException__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueJSONParseError__type_name,
  (void*) (ROGUEM3) RogueJSONParseError__init__String,
  (void*) (ROGUEM0) RogueException__display,
  (void*) (ROGUEM1) RogueException__format,
  (void*) (ROGUEM1) RogueJSONParseError___throw,
  (void*) (ROGUEM1) RogueJSONParserBuffer__init_object, // JSONParserBuffer
  (void*) (ROGUEM1) RogueStringBuilder__init,
  0, // StringBuilder.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueStringBuilder__to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Object.to_ValueTable()
  0, // Object.type_info()
  0, // Object.unpack(Value)
  (void*) (ROGUEM1) RogueJSONParserBuffer__type_name,
  (void*) (ROGUEM8) RogueStringBuilder__init__Int32,
  0, // StringBuilder.init(String)
  (void*) (ROGUEM1) RogueStringBuilder__clear,
  0, // StringBuilder.consolidated()
  0, // StringBuilder.contains(Character)
  0, // StringBuilder.discard_from(Int32)
  (void*) (ROGUEM43) RogueStringBuilder__get__Int32,
  0, // StringBuilder.insert(Character)
  0, // StringBuilder.insert_spaces(Int32,Int32)
  (void*) (ROGUEM44) RogueStringBuilder__locate__Character,
  0, // StringBuilder.locate_last(Character)
  (void*) (ROGUEM9) RogueStringBuilder__needs_indent,
  0, // StringBuilder.operator==(String)
  0, // StringBuilder.operator==(StringBuilder)
  0, // StringBuilder.print(Byte)
  (void*) (ROGUEM45) RogueStringBuilder__print__Character_Logical,
  (void*) (ROGUEM8) RogueStringBuilder__print__Int32,
  (void*) (ROGUEM38) RogueStringBuilder__print__Logical,
  (void*) (ROGUEM46) RogueStringBuilder__print__Int64,
  (void*) (ROGUEM3) RogueStringBuilder__print__Object,
  (void*) (ROGUEM39) RogueStringBuilder__print__Real64,
  (void*) (ROGUEM47) RogueStringBuilder__print__Real64_Int32,
  (void*) (ROGUEM3) RogueStringBuilder__print__String,
  (void*) (ROGUEM0) RogueStringBuilder__print_indent,
  (void*) (ROGUEM47) RogueStringBuilder__print_to_work_bytes__Real64_Int32,
  (void*) (ROGUEM0) RogueStringBuilder__print_work_bytes,
  (void*) (ROGUEM1) RogueStringBuilder__println,
  0, // StringBuilder.println(Byte)
  0, // StringBuilder.println(Character)
  0, // StringBuilder.println(Int32)
  0, // StringBuilder.println(Logical)
  0, // StringBuilder.println(Int64)
  0, // StringBuilder.println(Real64)
  0, // StringBuilder.println(Real64,Int32)
  0, // StringBuilder.println(Object)
  (void*) (ROGUEM3) RogueStringBuilder__println__String,
  0, // StringBuilder.remove_at(Int32)
  0, // StringBuilder.remove_last()
  (void*) (ROGUEM8) RogueStringBuilder__reserve__Int32,
  (void*) (ROGUEM0) RogueStringBuilder__round_off_work_bytes,
  (void*) (ROGUEM13) RogueStringBuilder__scan_work_bytes,
  (void*) (ROGUEM1) RogueOptionalBoxed_Byte___init_object, // Boxed<<Byte?>>
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueOptionalBoxed_Byte___to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Boxed<<Byte?>>.to_ValueTable()
  0, // Object.type_info()
  0, // Boxed<<Byte?>>.unpack(Value)
  (void*) (ROGUEM1) RogueOptionalBoxed_Byte___type_name,
  0, // Boxed<<Byte?>>.address()
  0, // Boxed<<Byte?>>.inner_introspector()
  0, // Boxed<<Byte?>>.size()
  0, // Boxed<<Byte?>>.type()
  (void*) (ROGUEM86) RogueOptionalBoxed_Byte___init__OptionalByte,
  (void*) (ROGUEM87) RogueOptionalOptionalBoxed_Byte___to_Byte,
  (void*) (ROGUEM1) RogueOptionalBoxed_Value___init_object, // Boxed<<Value?>>
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueOptionalBoxed_Value___to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Boxed<<Value?>>.to_ValueTable()
  0, // Object.type_info()
  0, // Boxed<<Value?>>.unpack(Value)
  (void*) (ROGUEM1) RogueOptionalBoxed_Value___type_name,
  0, // Boxed<<Value?>>.address()
  0, // Boxed<<Value?>>.inner_introspector()
  0, // Boxed<<Value?>>.size()
  0, // Boxed<<Value?>>.type()
  (void*) (ROGUEM88) RogueOptionalBoxed_Value___init__OptionalValue,
  (void*) (ROGUEM89) RogueOptionalOptionalBoxed_Value___to_Value,
  (void*) (ROGUEM1) RogueOptionalBoxed_StringBuilder___init_object, // Boxed<<StringBuilder?>>
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueOptionalBoxed_StringBuilder___to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Boxed<<StringBuilder?>>.to_ValueTable()
  0, // Object.type_info()
  0, // Boxed<<StringBuilder?>>.unpack(Value)
  (void*) (ROGUEM1) RogueOptionalBoxed_StringBuilder___type_name,
  0, // Boxed<<StringBuilder?>>.address()
  0, // Boxed<<StringBuilder?>>.inner_introspector()
  0, // Boxed<<StringBuilder?>>.size()
  0, // Boxed<<StringBuilder?>>.type()
  (void*) (ROGUEM90) RogueOptionalBoxed_StringBuilder___init__OptionalStringBuilder,
  (void*) (ROGUEM91) RogueOptionalOptionalBoxed_StringBuilder___to_StringBuilder,
  (void*) (ROGUEM1) RogueOptionalBoxed__Function______init_object, // Boxed<<(Function())?>>
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueOptionalBoxed__Function______to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Boxed<<(Function())?>>.to_ValueTable()
  0, // Object.type_info()
  0, // Boxed<<(Function())?>>.unpack(Value)
  (void*) (ROGUEM1) RogueOptionalBoxed__Function______type_name,
  0, // Boxed<<(Function())?>>.address()
  0, // Boxed<<(Function())?>>.inner_introspector()
  0, // Boxed<<(Function())?>>.size()
  0, // Boxed<<(Function())?>>.type()
  (void*) (ROGUEM92) RogueOptionalBoxed__Function______init__Optional_Function___,
  (void*) (ROGUEM93) RogueOptionalOptionalBoxed__Function______to__Function___,
  (void*) (ROGUEM1) RogueOptionalBoxed_String___init_object, // Boxed<<String?>>
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueOptionalBoxed_String___to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Boxed<<String?>>.to_ValueTable()
  0, // Object.type_info()
  0, // Boxed<<String?>>.unpack(Value)
  (void*) (ROGUEM1) RogueOptionalBoxed_String___type_name,
  0, // Boxed<<String?>>.address()
  0, // Boxed<<String?>>.inner_introspector()
  0, // Boxed<<String?>>.size()
  0, // Boxed<<String?>>.type()
  (void*) (ROGUEM94) RogueOptionalBoxed_String___init__OptionalString,
  (void*) (ROGUEM95) RogueOptionalOptionalBoxed_String___to_String,
  (void*) (ROGUEM1) RogueOptionalBoxed_PropertyInfo___init_object, // Boxed<<PropertyInfo?>>
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueOptionalBoxed_PropertyInfo___to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Boxed<<PropertyInfo?>>.to_ValueTable()
  0, // Object.type_info()
  0, // Boxed<<PropertyInfo?>>.unpack(Value)
  (void*) (ROGUEM1) RogueOptionalBoxed_PropertyInfo___type_name,
  0, // Boxed<<PropertyInfo?>>.address()
  0, // Boxed<<PropertyInfo?>>.inner_introspector()
  0, // Boxed<<PropertyInfo?>>.size()
  0, // Boxed<<PropertyInfo?>>.type()
  (void*) (ROGUEM96) RogueOptionalBoxed_PropertyInfo___init__OptionalPropertyInfo,
  (void*) (ROGUEM97) RogueOptionalOptionalBoxed_PropertyInfo___to_PropertyInfo,
  (void*) (ROGUEM1) RogueOptionalBoxed_MethodInfo___init_object, // Boxed<<MethodInfo?>>
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueOptionalBoxed_MethodInfo___to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Boxed<<MethodInfo?>>.to_ValueTable()
  0, // Object.type_info()
  0, // Boxed<<MethodInfo?>>.unpack(Value)
  (void*) (ROGUEM1) RogueOptionalBoxed_MethodInfo___type_name,
  0, // Boxed<<MethodInfo?>>.address()
  0, // Boxed<<MethodInfo?>>.inner_introspector()
  0, // Boxed<<MethodInfo?>>.size()
  0, // Boxed<<MethodInfo?>>.type()
  (void*) (ROGUEM98) RogueOptionalBoxed_MethodInfo___init__OptionalMethodInfo,
  (void*) (ROGUEM99) RogueOptionalOptionalBoxed_MethodInfo___to_MethodInfo,
  (void*) (ROGUEM1) RogueOptionalBoxed_Character___init_object, // Boxed<<Character?>>
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueOptionalBoxed_Character___to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Boxed<<Character?>>.to_ValueTable()
  0, // Object.type_info()
  0, // Boxed<<Character?>>.unpack(Value)
  (void*) (ROGUEM1) RogueOptionalBoxed_Character___type_name,
  0, // Boxed<<Character?>>.address()
  0, // Boxed<<Character?>>.inner_introspector()
  0, // Boxed<<Character?>>.size()
  0, // Boxed<<Character?>>.type()
  (void*) (ROGUEM100) RogueOptionalBoxed_Character___init__OptionalCharacter,
  (void*) (ROGUEM101) RogueOptionalOptionalBoxed_Character___to_Character,
  (void*) (ROGUEM1) RogueOptionalBoxed_Real64___init_object, // Boxed<<Real64?>>
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueOptionalBoxed_Real64___to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Boxed<<Real64?>>.to_ValueTable()
  0, // Object.type_info()
  0, // Boxed<<Real64?>>.unpack(Value)
  (void*) (ROGUEM1) RogueOptionalBoxed_Real64___type_name,
  0, // Boxed<<Real64?>>.address()
  0, // Boxed<<Real64?>>.inner_introspector()
  0, // Boxed<<Real64?>>.size()
  0, // Boxed<<Real64?>>.type()
  (void*) (ROGUEM102) RogueOptionalBoxed_Real64___init__OptionalReal64,
  (void*) (ROGUEM103) RogueOptionalOptionalBoxed_Real64___to_Real64,
  (void*) (ROGUEM1) RogueOptionalBoxed_Real32___init_object, // Boxed<<Real32?>>
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueOptionalBoxed_Real32___to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Boxed<<Real32?>>.to_ValueTable()
  0, // Object.type_info()
  0, // Boxed<<Real32?>>.unpack(Value)
  (void*) (ROGUEM1) RogueOptionalBoxed_Real32___type_name,
  0, // Boxed<<Real32?>>.address()
  0, // Boxed<<Real32?>>.inner_introspector()
  0, // Boxed<<Real32?>>.size()
  0, // Boxed<<Real32?>>.type()
  (void*) (ROGUEM104) RogueOptionalBoxed_Real32___init__OptionalReal32,
  (void*) (ROGUEM105) RogueOptionalOptionalBoxed_Real32___to_Real32,
  (void*) (ROGUEM1) RogueOptionalBoxed_Int64___init_object, // Boxed<<Int64?>>
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueOptionalBoxed_Int64___to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Boxed<<Int64?>>.to_ValueTable()
  0, // Object.type_info()
  0, // Boxed<<Int64?>>.unpack(Value)
  (void*) (ROGUEM1) RogueOptionalBoxed_Int64___type_name,
  0, // Boxed<<Int64?>>.address()
  0, // Boxed<<Int64?>>.inner_introspector()
  0, // Boxed<<Int64?>>.size()
  0, // Boxed<<Int64?>>.type()
  (void*) (ROGUEM106) RogueOptionalBoxed_Int64___init__OptionalInt64,
  (void*) (ROGUEM107) RogueOptionalOptionalBoxed_Int64___to_Int64,
  (void*) (ROGUEM1) RogueBoxed_FileOptions___init_object, // Boxed<<FileOptions>>
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueBoxed_FileOptions___to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Boxed<<FileOptions>>.to_ValueTable()
  0, // Object.type_info()
  0, // Boxed<<FileOptions>>.unpack(Value)
  (void*) (ROGUEM1) RogueBoxed_FileOptions___type_name,
  0, // Boxed<<FileOptions>>.address()
  0, // Boxed<<FileOptions>>.inner_introspector()
  0, // Boxed<<FileOptions>>.size()
  0, // Boxed<<FileOptions>>.type()
  (void*) (ROGUEM108) RogueBoxed_FileOptions___init__FileOptions,
  (void*) (ROGUEM109) RogueBoxed_FileOptions___to_FileOptions,
  (void*) (ROGUEM1) RogueOptionalBoxed_TableEntry_String_Value____init_object, // Boxed<<TableEntry<<String,Value>>?>>
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueOptionalBoxed_TableEntry_String_Value____to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Boxed<<TableEntry<<String,Value>>?>>.to_ValueTable()
  0, // Object.type_info()
  0, // Boxed<<TableEntry<<String,Value>>?>>.unpack(Value)
  (void*) (ROGUEM1) RogueOptionalBoxed_TableEntry_String_Value____type_name,
  0, // Boxed<<TableEntry<<String,Value>>?>>.address()
  0, // Boxed<<TableEntry<<String,Value>>?>>.inner_introspector()
  0, // Boxed<<TableEntry<<String,Value>>?>>.size()
  0, // Boxed<<TableEntry<<String,Value>>?>>.type()
  (void*) (ROGUEM110) RogueOptionalBoxed_TableEntry_String_Value____init__OptionalTableEntry_String_Value_,
  (void*) (ROGUEM111) RogueOptionalOptionalBoxed_TableEntry_String_Value____to_TableEntry_String_Value_,
  (void*) (ROGUEM1) RogueOptionalBoxed_TypeInfo___init_object, // Boxed<<TypeInfo?>>
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueOptionalBoxed_TypeInfo___to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Boxed<<TypeInfo?>>.to_ValueTable()
  0, // Object.type_info()
  0, // Boxed<<TypeInfo?>>.unpack(Value)
  (void*) (ROGUEM1) RogueOptionalBoxed_TypeInfo___type_name,
  0, // Boxed<<TypeInfo?>>.address()
  0, // Boxed<<TypeInfo?>>.inner_introspector()
  0, // Boxed<<TypeInfo?>>.size()
  0, // Boxed<<TypeInfo?>>.type()
  (void*) (ROGUEM112) RogueOptionalBoxed_TypeInfo___init__OptionalTypeInfo,
  (void*) (ROGUEM113) RogueOptionalOptionalBoxed_TypeInfo___to_TypeInfo,
  (void*) (ROGUEM1) RogueOptionalBoxed_TableEntry_Int32_String____init_object, // Boxed<<TableEntry<<Int32,String>>?>>
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueOptionalBoxed_TableEntry_Int32_String____to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Boxed<<TableEntry<<Int32,String>>?>>.to_ValueTable()
  0, // Object.type_info()
  0, // Boxed<<TableEntry<<Int32,String>>?>>.unpack(Value)
  (void*) (ROGUEM1) RogueOptionalBoxed_TableEntry_Int32_String____type_name,
  0, // Boxed<<TableEntry<<Int32,String>>?>>.address()
  0, // Boxed<<TableEntry<<Int32,String>>?>>.inner_introspector()
  0, // Boxed<<TableEntry<<Int32,String>>?>>.size()
  0, // Boxed<<TableEntry<<Int32,String>>?>>.type()
  (void*) (ROGUEM114) RogueOptionalBoxed_TableEntry_Int32_String____init__OptionalTableEntry_Int32_String_,
  (void*) (ROGUEM115) RogueOptionalOptionalBoxed_TableEntry_Int32_String____to_TableEntry_Int32_String_,
  (void*) (ROGUEM1) RogueOptionalBoxed_TableEntry_String_String____init_object, // Boxed<<TableEntry<<String,String>>?>>
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueOptionalBoxed_TableEntry_String_String____to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Boxed<<TableEntry<<String,String>>?>>.to_ValueTable()
  0, // Object.type_info()
  0, // Boxed<<TableEntry<<String,String>>?>>.unpack(Value)
  (void*) (ROGUEM1) RogueOptionalBoxed_TableEntry_String_String____type_name,
  0, // Boxed<<TableEntry<<String,String>>?>>.address()
  0, // Boxed<<TableEntry<<String,String>>?>>.inner_introspector()
  0, // Boxed<<TableEntry<<String,String>>?>>.size()
  0, // Boxed<<TableEntry<<String,String>>?>>.type()
  (void*) (ROGUEM116) RogueOptionalBoxed_TableEntry_String_String____init__OptionalTableEntry_String_String_,
  (void*) (ROGUEM117) RogueOptionalOptionalBoxed_TableEntry_String_String____to_TableEntry_String_String_,
  (void*) (ROGUEM1) RogueOptionalBoxed_TableEntry_String_TypeInfo____init_object, // Boxed<<TableEntry<<String,TypeInfo>>?>>
  (void*) (ROGUEM1) RogueObject__init,
  0, // Object.hash_code()
  0, // Object.introspector()
  (void*) (ROGUEM2) RogueObject__object_id,
  0, // Object.operator==(Object)
  (void*) (ROGUEM1) RogueOptionalBoxed_TableEntry_String_TypeInfo____to_String,
  0, // Object.to_Value()
  0, // Object.to_ValueList()
  0, // Boxed<<TableEntry<<String,TypeInfo>>?>>.to_ValueTable()
  0, // Object.type_info()
  0, // Boxed<<TableEntry<<String,TypeInfo>>?>>.unpack(Value)
  (void*) (ROGUEM1) RogueOptionalBoxed_TableEntry_String_TypeInfo____type_name,
  0, // Boxed<<TableEntry<<String,TypeInfo>>?>>.address()
  0, // Boxed<<TableEntry<<String,TypeInfo>>?>>.inner_introspector()
  0, // Boxed<<TableEntry<<String,TypeInfo>>?>>.size()
  0, // Boxed<<TableEntry<<String,TypeInfo>>?>>.type()
  (void*) (ROGUEM118) RogueOptionalBoxed_TableEntry_String_TypeInfo____init__OptionalTableEntry_String_TypeInfo_,
  (void*) (ROGUEM119) RogueOptionalOptionalBoxed_TableEntry_String_TypeInfo____to_TableEntry_String_TypeInfo_,
  (void*) (ROGUEM120) RogueOptionalInt32__to_Value, // Int32?
  (void*) (ROGUEM120) RogueOptionalInt32__to_String,
  (void*) (ROGUEM122) RogueSystemEnvironment__to_Value, // SystemEnvironment
  (void*) (ROGUEM123) RogueSystemEnvironment__get__String,
  (void*) (ROGUEM122) RogueSystemEnvironment__to_String,
  (void*) (ROGUEM124) RogueOptionalByte__to_Value, // Byte?
  (void*) (ROGUEM124) RogueOptionalByte__to_String,
  (void*) (ROGUEM126) RogueOptionalValue__to_Value, // Value?
  (void*) (ROGUEM126) RogueOptionalValue__to_String,
  (void*) (ROGUEM127) RogueOptionalStringBuilder__to_Value, // StringBuilder?
  (void*) (ROGUEM127) RogueOptionalStringBuilder__to_String,
  (void*) (ROGUEM128) RogueOptional_Function_____to_Value, // (Function())?
  (void*) (ROGUEM128) RogueOptional_Function_____to_String,
  (void*) (ROGUEM129) RogueOptionalString__to_Value, // String?
  (void*) (ROGUEM129) RogueOptionalString__to_String,
  (void*) (ROGUEM130) RogueOptionalPropertyInfo__to_Value, // PropertyInfo?
  (void*) (ROGUEM130) RogueOptionalPropertyInfo__to_String,
  (void*) (ROGUEM131) RogueOptionalMethodInfo__to_Value, // MethodInfo?
  (void*) (ROGUEM131) RogueOptionalMethodInfo__to_String,
  (void*) (ROGUEM132) RogueOptionalCharacter__to_Value, // Character?
  (void*) (ROGUEM132) RogueOptionalCharacter__to_String,
  (void*) (ROGUEM134) RogueOptionalReal64__to_Value, // Real64?
  (void*) (ROGUEM134) RogueOptionalReal64__to_String,
  (void*) (ROGUEM135) RogueOptionalReal32__to_Value, // Real32?
  (void*) (ROGUEM135) RogueOptionalReal32__to_String,
  (void*) (ROGUEM136) RogueOptionalInt64__to_Value, // Int64?
  (void*) (ROGUEM136) RogueOptionalInt64__to_String,
  (void*) (ROGUEM137) RogueFileOptions__to_Value, // FileOptions
  (void*) (ROGUEM137) RogueFileOptions__to_String,
  (void*) (ROGUEM138) RogueOptionalTableEntry_String_Value___to_Value, // TableEntry<<String,Value>>?
  (void*) (ROGUEM138) RogueOptionalTableEntry_String_Value___to_String,
  (void*) (ROGUEM139) RogueOptionalTypeInfo__to_Value, // TypeInfo?
  (void*) (ROGUEM139) RogueOptionalTypeInfo__to_String,
  (void*) (ROGUEM140) RogueOptionalTableEntry_Int32_String___to_Value, // TableEntry<<Int32,String>>?
  (void*) (ROGUEM140) RogueOptionalTableEntry_Int32_String___to_String,
  (void*) (ROGUEM141) RogueOptionalTableEntry_String_String___to_Value, // TableEntry<<String,String>>?
  (void*) (ROGUEM141) RogueOptionalTableEntry_String_String___to_String,
  (void*) (ROGUEM142) RogueOptionalTableEntry_String_TypeInfo___to_Value, // TableEntry<<String,TypeInfo>>?
  (void*) (ROGUEM142) RogueOptionalTableEntry_String_TypeInfo___to_String,

};

const int Rogue_type_info_table[] ={
  // allocator_index, dynamic_method_table_index, base_type_count, base_type_index[base_type_count],
  // global_property_count, global_property_name_indices[global_property_count], global_property_type_indices[global_property_count],
  // property_count, property_name_indices[property_count], property_type_indices[property_count] ...
  6,0,0,0,0,0,13,19,0,13,3,0,2,3,0,5,56,289,290,291,292,4,5,3,13,22,38,
  7,0,51,1,3,0,0,26,6,0,77,0,0,0,26,10,0,103,2,5,0,0,1,293,6,116,7,0,219,
  1,0,0,0,116,23,0,335,1,0,0,8,294,295,296,297,298,299,300,301,7,7,7,8,10,10,
  10,12,24,6,0,0,0,0,0,0,8,0,363,2,9,0,0,0,14,7,0,377,1,0,0,0,14,19,0,
  391,1,0,0,6,302,227,303,304,305,306,11,5,10,10,10,7,14,7,0,405,1,0,0,0,54,
  7,0,459,1,0,0,0,14,23,0,473,1,0,2,307,308,14,18,6,309,294,310,311,312,313,14,7,
  7,7,7,21,54,12,0,527,2,15,0,0,2,293,294,17,7,28,7,0,555,1,0,0,0,13,6,0,
  0,0,0,0,0,8,0,569,2,9,0,0,0,14,9,0,583,1,0,0,1,314,19,13,12,0,596,2,
  15,0,0,2,293,294,20,7,14,8,0,610,2,9,0,0,0,14,6,0,0,0,0,0,0,12,0,625,
  2,15,0,0,2,293,294,24,7,17,7,0,642,1,0,0,0,14,8,0,656,2,9,0,0,0,14,
  11,0,670,1,0,0,2,315,316,11,26,16,13,0,686,1,0,0,3,317,294,318,27,7,21,17,12,0,
  703,2,15,0,0,2,293,294,28,7,22,8,0,725,2,9,0,0,0,14,6,0,0,0,0,0,0,
  6,0,0,0,0,0,0,6,0,0,0,0,0,0,6,0,0,0,0,0,0,17,0,754,1,0,0,5,319,
  59,320,321,322,7,11,34,34,37,17,12,0,771,2,15,0,0,2,293,294,36,7,17,13,0,788,1,
  0,0,3,319,323,324,7,7,7,16,8,0,804,2,9,0,0,0,14,12,0,818,2,15,0,0,2,
  293,294,43,7,17,29,0,835,1,0,1,325,39,10,319,59,326,327,328,329,330,331,332,333,7,11,
  11,11,31,7,7,7,7,7,19,23,0,854,1,0,0,8,294,295,296,297,298,299,300,301,7,7,
  7,40,41,41,41,42,22,8,0,876,2,9,0,0,0,14,19,0,890,1,0,0,6,302,227,303,304,
  305,306,7,11,41,41,41,7,14,7,0,904,1,0,0,0,14,8,0,918,2,9,0,0,0,14,12,0,
  932,2,15,0,0,2,293,294,45,7,19,8,0,951,2,9,0,0,0,14,10,0,965,2,5,0,0,
  1,293,47,118,12,0,1083,2,15,0,0,2,293,294,48,7,20,8,0,1103,2,9,0,0,0,14,10,0,
  1117,2,5,0,0,1,227,0,118,25,0,1235,3,51,52,0,0,8,294,295,296,297,298,299,300,301,
  7,7,7,53,54,54,54,55,50,24,0,1285,2,52,0,0,8,294,295,296,297,298,299,300,301,7,
  7,7,53,54,54,54,55,50,23,0,1335,1,0,0,8,294,295,296,297,298,299,300,301,7,7,7,
  53,54,54,54,55,50,8,0,1385,2,9,0,0,0,14,19,0,1399,1,0,0,6,302,227,303,304,305,
  306,11,11,54,54,54,7,14,7,0,1413,1,0,0,0,14,8,0,1427,0,0,1,334,7,7,8,0,1434,
  0,0,1,334,7,1,32,0,1435,4,0,56,59,3,0,11,334,335,336,337,338,339,340,341,342,343,
  344,7,60,21,21,21,13,13,117,14,31,31,24,7,0,1459,1,3,0,0,26,11,0,1485,3,0,59,
  3,0,1,339,13,19,20,0,1504,2,13,0,0,6,309,294,310,311,312,313,14,7,7,7,7,21,
  54,7,0,1558,1,0,0,0,13,7,0,1571,1,0,0,0,13,8,0,1584,2,23,0,0,0,14,13,0,1598,
  1,0,3,345,346,347,27,11,118,0,13,12,0,1611,2,25,0,0,2,315,316,11,26,17,9,0,1628,
  1,0,0,1,17,11,15,18,0,1643,2,0,57,0,5,334,348,349,350,351,7,56,11,13,32,20,
  8,0,1663,0,0,1,334,7,7,18,0,1670,2,0,71,0,5,334,17,335,350,352,7,11,21,14,31,
  21,8,0,1691,0,0,1,334,7,11,20,0,1702,2,0,56,0,6,334,293,294,353,354,355,7,44,7,
  7,7,7,22,9,0,1724,1,0,0,1,356,72,30,7,0,1754,1,0,0,0,13,7,0,1767,1,0,0,
  0,14,8,0,1781,2,75,0,0,0,14,7,0,1795,1,0,0,0,13,11,0,1808,1,0,0,2,357,358,
  78,31,14,13,0,1822,3,0,80,3,0,2,359,350,71,13,20,7,0,1842,1,3,0,0,26,14,0,1868,
  2,5,0,2,360,361,81,81,1,227,21,118,10,0,1986,2,5,0,0,1,227,7,118,10,0,2104,2,
  5,0,0,1,227,31,118,10,0,2222,2,5,0,0,1,227,29,118,8,0,2340,2,5,0,0,0,116,
  12,0,2456,2,5,0,1,362,86,1,227,11,118,9,0,2574,3,85,5,0,0,0,116,13,0,2690,3,66,
  25,0,0,2,315,316,11,26,18,19,0,2708,1,0,0,6,302,227,303,304,305,306,11,33,89,89,
  89,7,13,13,0,2721,3,66,25,0,0,2,315,316,11,26,17,13,0,2738,1,0,0,3,363,364,365,
  44,7,7,17,10,0,2755,2,63,0,0,1,227,117,19,10,0,2774,2,23,0,0,1,290,58,15,10,0,
  2789,2,63,0,0,1,227,118,19,13,0,2808,3,66,25,0,0,2,315,316,11,26,17,20,0,2825,2,
  0,69,0,6,334,17,294,366,350,352,7,11,7,7,14,31,20,14,0,2845,2,0,56,0,3,334,
  367,349,7,69,126,18,13,0,2863,3,66,25,0,0,2,315,316,11,26,17,20,0,2880,2,13,0,0,
  6,309,294,310,311,312,313,14,7,7,7,7,21,54,10,0,2934,2,63,0,0,1,227,119,19,10,0,
  2953,2,63,0,0,1,227,120,19,10,0,2972,2,63,0,0,1,227,121,19,10,0,2991,2,63,0,0,
  1,227,122,19,10,0,3010,2,63,0,0,1,227,123,19,10,0,3029,2,63,0,0,1,227,124,19,10,0,
  3048,2,63,0,0,1,227,125,19,10,0,3067,2,63,0,0,1,227,126,19,10,0,3086,2,63,0,0,
  1,227,127,19,10,0,3105,2,63,0,0,1,227,128,19,10,0,3124,2,63,0,0,1,227,129,19,10,0,
  3143,2,63,0,0,1,227,130,19,10,0,3162,2,63,0,0,1,227,131,19,10,0,3181,2,63,0,0,
  1,227,132,19,10,0,3200,2,63,0,0,1,227,133,19,10,0,3219,2,63,0,0,1,227,134,19,10,0,
  3238,2,63,0,0,1,227,135,19,10,0,3257,0,0,2,227,228,7,21,2,6,0,3259,0,0,0,3,
  10,0,3262,0,0,2,227,228,16,21,2,10,0,3264,0,0,2,227,228,5,21,2,10,0,3266,0,0,2,
  227,228,13,21,2,10,0,3268,0,0,2,227,228,23,21,2,10,0,3270,0,0,2,227,228,11,21,2,
  10,0,3272,0,0,2,227,228,35,21,2,10,0,3274,0,0,2,227,228,38,21,2,10,0,3276,0,0,2,
  227,228,32,21,2,10,0,3278,0,0,2,227,228,29,21,2,10,0,3280,0,0,2,227,228,30,21,2,
  10,0,3282,0,0,2,227,228,31,21,2,8,0,3284,0,0,1,254,7,2,10,0,3286,0,0,2,227,228,
  10,21,2,10,0,3288,0,0,2,227,228,33,21,2,10,0,3290,0,0,2,227,228,41,21,2,10,0,3292,
  0,0,2,227,228,54,21,2,10,0,3294,0,0,2,227,228,89,21,2,
};

const char* Rogue_method_name_strings[] =
{
  /* 0 */ "init_object",
  /* 1 */ "init",
  /* 2 */ "object_id",
  /* 3 */ "to_String",
  /* 4 */ "type_name",
  /* 5 */ "close",
  /* 6 */ "flush",
  /* 7 */ "print",
  /* 8 */ "println",
  /* 9 */ "write",
  /* 10 */ "prep_arg",
  /* 11 */ "require_command_line",
  /* 12 */ "save_cache",
  /* 13 */ "install_library_manager",
  /* 14 */ "install_brew",
  /* 15 */ "install_library",
  /* 16 */ "install_macos_library",
  /* 17 */ "find_path",
  /* 18 */ "install_ubuntu_library",
  /* 19 */ "library_location",
  /* 20 */ "header_location",
  /* 21 */ "scan_config",
  /* 22 */ "parse_filepath",
  /* 23 */ "on_launch",
  /* 24 */ "run_tests",
  /* 25 */ "call_exit_functions",
  /* 26 */ "on_exit",
  /* 27 */ "contains",
  /* 28 */ "count",
  /* 29 */ "ensure_list",
  /* 30 */ "ensure_table",
  /* 31 */ "get",
  /* 32 */ "is_collection",
  /* 33 */ "is_complex",
  /* 34 */ "is_int32",
  /* 35 */ "is_int64",
  /* 36 */ "is_logical",
  /* 37 */ "is_null",
  /* 38 */ "is_non_null",
  /* 39 */ "is_number",
  /* 40 */ "is_object",
  /* 41 */ "is_string",
  /* 42 */ "keys",
  /* 43 */ "operator==",
  /* 44 */ "remove",
  /* 45 */ "save",
  /* 46 */ "set",
  /* 47 */ "to_Byte",
  /* 48 */ "to_Character",
  /* 49 */ "to_Int64",
  /* 50 */ "to_Int32",
  /* 51 */ "to_Logical",
  /* 52 */ "to_Real64",
  /* 53 */ "to_Real32",
  /* 54 */ "to_Object",
  /* 55 */ "to_json",
  /* 56 */ "find",
  /* 57 */ "print_to",
  /* 58 */ "_adjust_entry_order",
  /* 59 */ "_place_entry_in_order",
  /* 60 */ "_unlink",
  /* 61 */ "_grow",
  /* 62 */ "hash_code",
  /* 63 */ "or_smaller",
  /* 64 */ "to_digit",
  /* 65 */ "zero",
  /* 66 */ "after",
  /* 67 */ "after_first",
  /* 68 */ "after_last",
  /* 69 */ "before",
  /* 70 */ "before_first",
  /* 71 */ "before_last",
  /* 72 */ "begins_with",
  /* 73 */ "consolidated",
  /* 74 */ "contains_at",
  /* 75 */ "ends_with",
  /* 76 */ "from",
  /* 77 */ "from_first",
  /* 78 */ "last",
  /* 79 */ "left_justified",
  /* 80 */ "leftmost",
  /* 81 */ "locate",
  /* 82 */ "locate_last",
  /* 83 */ "operator+",
  /* 84 */ "pluralized",
  /* 85 */ "replacing",
  /* 86 */ "rightmost",
  /* 87 */ "split",
  /* 88 */ "times",
  /* 89 */ "to_lowercase",
  /* 90 */ "trimmed",
  /* 91 */ "word_wrap",
  /* 92 */ "call",
  /* 93 */ "clear",
  /* 94 */ "needs_indent",
  /* 95 */ "print_indent",
  /* 96 */ "print_to_work_bytes",
  /* 97 */ "print_work_bytes",
  /* 98 */ "reserve",
  /* 99 */ "round_off_work_bytes",
  /* 100 */ "scan_work_bytes",
  /* 101 */ "add",
  /* 102 */ "capacity",
  /* 103 */ "ensure_capacity",
  /* 104 */ "expand_to_count",
  /* 105 */ "expand_to_include",
  /* 106 */ "discard_from",
  /* 107 */ "insert",
  /* 108 */ "remove_at",
  /* 109 */ "remove_last",
  /* 110 */ "reverse",
  /* 111 */ "shift",
  /* 112 */ "display",
  /* 113 */ "format",
  /* 114 */ "join",
  /* 115 */ "mapped<<String>>",
  /* 116 */ "decimal_digit_count",
  /* 117 */ "fractional_part",
  /* 118 */ "is_infinite",
  /* 119 */ "is_not_a_number",
  /* 120 */ "whole_part",
  /* 121 */ "print_in_power2_base",
  /* 122 */ "to_hex_string",
  /* 123 */ "is_identifier",
  /* 124 */ "is_letter",
  /* 125 */ "to_number",
  /* 126 */ "add_global_property_info",
  /* 127 */ "add_property_info",
  /* 128 */ "add_method_info",
  /* 129 */ "name",
  /* 130 */ "type",
  /* 131 */ "return_type",
  /* 132 */ "parameter_name",
  /* 133 */ "parameter_type",
  /* 134 */ "signature",
  /* 135 */ "rewriter",
  /* 136 */ "has_another",
  /* 137 */ "read",
  /* 138 */ "fill_input_queue",
  /* 139 */ "reset_input_mode",
  /* 140 */ "width",
  /* 141 */ "_throw",
  /* 142 */ "filename",
  /* 143 */ "prepare_next",
  /* 144 */ "on_cleanup",
  /* 145 */ "open",
  /* 146 */ "peek",
  /* 147 */ "consume",
  /* 148 */ "consume_eols",
  /* 149 */ "consume_spaces",
  /* 150 */ "convert_crlf_to_newline",
  /* 151 */ "next_is",
  /* 152 */ "parse_value",
  /* 153 */ "parse_table",
  /* 154 */ "parse_list",
  /* 155 */ "parse_string",
  /* 156 */ "parse_hex_quad",
  /* 157 */ "parse_identifier",
  /* 158 */ "next_is_identifier",
  /* 159 */ "parse_number",
  /* 160 */ "consume_spaces_and_eols",
  /* 161 */ "to_Int32?",
  /* 162 */ "to_SystemEnvironment",
  /* 163 */ "to_Byte?",
  /* 164 */ "to_Value?",
  /* 165 */ "to_StringBuilder?",
  /* 166 */ "to_(Function())?",
  /* 167 */ "to_String?",
  /* 168 */ "to_PropertyInfo?",
  /* 169 */ "to_MethodInfo?",
  /* 170 */ "to_Character?",
  /* 171 */ "to_Real64?",
  /* 172 */ "to_Real32?",
  /* 173 */ "to_Int64?",
  /* 174 */ "to_FileOptions",
  /* 175 */ "to_TableEntry<<String,Value>>?",
  /* 176 */ "to_TypeInfo?",
  /* 177 */ "to_TableEntry<<Int32,String>>?",
  /* 178 */ "to_TableEntry<<String,String>>?",
  /* 179 */ "to_TableEntry<<String,TypeInfo>>?",
  /* 180 */ "to_Value",
};

const int Rogue_method_param_names[] =
{
  227,350,368,93,93,369,370,371,372,302,319,373,370,374,375,302,376,374,375,350,374,375,350,254,377,
  302,227,378,363,379,380,381,382,383,384,385,386,387,387,388,380,380,389,390,391,381,385,392,373,392,
  385,393,394,395,396,397,398,399,398,350,399,400,401,402,227,374,227,403,404,405,406,227,407,408,380,
  409,410,391,411,412,413,414,415,416,350,416,417,418,415,419,420,421,422,323,324,423,424,425,426,427,
  428,429,430,431,432,433,434,435,356,436,437,438,439,438,293,348,440,441,442,443,444,445,400,446,447,
  448,449,450,451,452,453,319,454,455,456,457,458,459,437,460,461,462,463,464,465,466,467,468,469,470,
  471,472,473,474,475,476,477,478,479,59,
};

const int Rogue_method_param_types[] =
{
  0,11,13,5,5,11,67,73,23,7,67,21,21,11,5,21,21,13,21,21,13,7,27,10,7,
  7,11,5,7,32,11,7,7,32,32,117,11,117,21,29,11,11,7,11,7,13,11,10,10,32,
  21,31,29,7,16,16,7,7,117,7,119,75,117,7,7,13,35,7,7,7,38,7,7,7,7,
  7,7,41,7,11,7,41,41,54,11,11,7,54,54,56,69,14,11,21,11,7,21,72,32,32,
  71,44,58,118,119,120,121,122,123,124,125,126,127,128,129,130,131,132,133,134,135,
};

// name_string, arg_count, first_param_name, first_param_type, return_type, introspection call handler index
const int Rogue_method_info_table[][6] =
{
  {0,0,0,0,-1,11}, // Object.init_object()
  {1,0,0,0,0,1}, // Object.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // Object.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // Object.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // Object.type_name()
  {0,0,0,0,1,1}, // Global.init_object()
  {1,0,0,0,1,1}, // Global.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // Global.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // Global.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // Global.type_name()
  {5,0,0,0,1,1}, // Global.close()
  {6,0,0,0,1,1}, // Global.flush()
  {7,1,0,0,1,9}, // Global.print(Object)
  {7,1,0,1,1,9}, // Global.print(String)
  {8,0,0,0,1,1}, // Global.println()
  {8,1,0,0,1,9}, // Global.println(Object)
  {8,1,0,1,1,9}, // Global.println(String)
  {9,1,1,2,1,9}, // Global.write(StringBuilder)
  {10,1,2,1,11,9}, // Global.prep_arg(String)
  {11,0,0,0,-1,11}, // Global.require_command_line()
  {12,0,0,0,-1,11}, // Global.save_cache()
  {13,0,0,0,-1,11}, // Global.install_library_manager()
  {14,0,0,0,-1,11}, // Global.install_brew()
  {15,1,3,3,-1,15}, // Global.install_library(Value)
  {16,1,3,3,-1,15}, // Global.install_macos_library(Value)
  {17,2,4,4,11,71}, // Global.find_path(Value,String)
  {18,1,3,3,-1,15}, // Global.install_ubuntu_library(Value)
  {19,1,3,3,11,72}, // Global.library_location(Value)
  {20,1,3,3,11,72}, // Global.header_location(Value)
  {21,1,6,6,-1,73}, // Global.scan_config(File)
  {22,1,7,7,11,9}, // Global.parse_filepath(JSONParser)
  {23,0,0,0,-1,11}, // Global.on_launch()
  {24,0,0,0,-1,11}, // Global.run_tests()
  {25,0,0,0,-1,11}, // Global.call_exit_functions()
  {26,1,8,8,-1,73}, // Global.on_exit((Function()))
  {5,0,0,0,2,1}, // PrintWriter<<global_output_buffer>>.close()
  {6,0,0,0,2,1}, // PrintWriter<<global_output_buffer>>.flush()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {8,1,0,1,2,9}, // PrintWriter<<global_output_buffer>>.println(String)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {0,0,0,0,4,81}, // ValueTable.init_object()
  {1,0,0,0,4,81}, // ValueTable.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // ValueTable.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,0}, // ValueTable.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,0}, // ValueTable.type_name()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {27,1,9,1,21,82}, // ValueTable.contains(String)
  {27,1,9,3,21,85}, // ValueTable.contains(Value)
  {-1,-1,-1,-1,-1,1},
  {28,0,0,0,7,47}, // ValueTable.count()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {29,1,9,1,46,84}, // ValueTable.ensure_list(String)
  {30,1,9,1,4,84}, // ValueTable.ensure_table(String)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {31,1,10,9,5,86}, // ValueTable.get(Int32)
  {31,1,9,1,5,84}, // ValueTable.get(String)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {32,0,0,0,21,64}, // ValueTable.is_collection()
  {33,0,0,0,21,64}, // ValueTable.is_complex()
  {34,0,0,0,21,64}, // ValueTable.is_int32()
  {35,0,0,0,21,64}, // ValueTable.is_int64()
  {-1,-1,-1,-1,-1,1},
  {36,0,0,0,21,64}, // ValueTable.is_logical()
  {37,0,0,0,21,64}, // ValueTable.is_null()
  {38,0,0,0,21,64}, // ValueTable.is_non_null()
  {39,0,0,0,21,64}, // ValueTable.is_number()
  {40,0,0,0,21,64}, // ValueTable.is_object()
  {-1,-1,-1,-1,-1,1},
  {41,0,0,0,21,64}, // ValueTable.is_string()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {42,0,0,0,27,0}, // ValueTable.keys()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {43,1,11,3,21,85}, // ValueTable.operator==(Value)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {43,1,11,1,21,82}, // ValueTable.operator==(String)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {44,1,0,3,5,83}, // ValueTable.remove(Value)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {45,3,12,10,21,88}, // ValueTable.save(File,Logical,Logical)
  {-1,-1,-1,-1,-1,1},
  {46,2,15,13,4,89}, // ValueTable.set(String,Value)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {47,0,0,0,16,61}, // ValueTable.to_Byte()
  {48,0,0,0,32,54}, // ValueTable.to_Character()
  {49,0,0,0,31,40}, // ValueTable.to_Int64()
  {50,0,0,0,7,47}, // ValueTable.to_Int32()
  {51,0,0,0,21,64}, // ValueTable.to_Logical()
  {52,0,0,0,29,28}, // ValueTable.to_Real64()
  {53,0,0,0,30,37}, // ValueTable.to_Real32()
  {54,0,0,0,0,0}, // ValueTable.to_Object()
  {-1,-1,-1,-1,-1,1},
  {55,2,17,15,11,90}, // ValueTable.to_json(Logical,Logical)
  {55,3,19,17,13,91}, // ValueTable.to_json(StringBuilder,Logical,Logical)
  {55,2,22,20,13,92}, // ValueTable.to_json(StringBuilder,Int32)
  {0,0,0,0,5,81}, // Value.init_object()
  {1,0,0,0,0,1}, // Value.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // Value.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // Value.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,0}, // Value.type_name()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {27,1,24,1,21,82}, // Value.contains(String)
  {27,1,24,3,21,85}, // Value.contains(Value)
  {-1,-1,-1,-1,-1,1},
  {28,0,0,0,7,47}, // Value.count()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {30,1,9,1,4,84}, // Value.ensure_table(String)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {31,1,10,9,5,86}, // Value.get(Int32)
  {31,1,9,1,5,84}, // Value.get(String)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {32,0,0,0,21,64}, // Value.is_collection()
  {33,0,0,0,21,64}, // Value.is_complex()
  {34,0,0,0,21,64}, // Value.is_int32()
  {35,0,0,0,21,64}, // Value.is_int64()
  {-1,-1,-1,-1,-1,1},
  {36,0,0,0,21,64}, // Value.is_logical()
  {37,0,0,0,21,64}, // Value.is_null()
  {38,0,0,0,21,64}, // Value.is_non_null()
  {39,0,0,0,21,64}, // Value.is_number()
  {40,0,0,0,21,64}, // Value.is_object()
  {-1,-1,-1,-1,-1,1},
  {41,0,0,0,21,64}, // Value.is_string()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {43,1,11,3,21,85}, // Value.operator==(Value)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {43,1,11,1,21,82}, // Value.operator==(String)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {44,1,0,3,5,83}, // Value.remove(Value)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {45,3,12,10,21,88}, // Value.save(File,Logical,Logical)
  {-1,-1,-1,-1,-1,1},
  {46,2,25,13,5,89}, // Value.set(String,Value)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {47,0,0,0,16,61}, // Value.to_Byte()
  {48,0,0,0,32,54}, // Value.to_Character()
  {49,0,0,0,31,40}, // Value.to_Int64()
  {50,0,0,0,7,47}, // Value.to_Int32()
  {51,0,0,0,21,64}, // Value.to_Logical()
  {52,0,0,0,29,28}, // Value.to_Real64()
  {53,0,0,0,30,37}, // Value.to_Real32()
  {54,0,0,0,0,0}, // Value.to_Object()
  {-1,-1,-1,-1,-1,1},
  {55,2,17,15,11,90}, // Value.to_json(Logical,Logical)
  {55,3,19,17,13,91}, // Value.to_json(StringBuilder,Logical,Logical)
  {55,2,22,20,13,92}, // Value.to_json(StringBuilder,Int32)
  {0,0,0,0,6,1}, // Table<<String,Value>>.init_object()
  {1,0,0,0,6,1}, // Table<<String,Value>>.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // Table<<String,Value>>.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // Table<<String,Value>>.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // Table<<String,Value>>.type_name()
  {1,1,27,9,6,6}, // Table<<String,Value>>.init(Int32)
  {27,1,9,1,21,3}, // Table<<String,Value>>.contains(String)
  {56,1,9,1,10,9}, // Table<<String,Value>>.find(String)
  {31,1,9,1,5,95}, // Table<<String,Value>>.get(String)
  {42,1,28,22,27,9}, // Table<<String,Value>>.keys(String[])
  {57,1,1,2,13,9}, // Table<<String,Value>>.print_to(StringBuilder)
  {46,2,25,13,6,96}, // Table<<String,Value>>.set(String,Value)
  {58,1,29,23,-1,73}, // Table<<String,Value>>._adjust_entry_order(TableEntry<<String,Value>>)
  {59,1,29,23,-1,73}, // Table<<String,Value>>._place_entry_in_order(TableEntry<<String,Value>>)
  {60,1,29,23,-1,73}, // Table<<String,Value>>._unlink(TableEntry<<String,Value>>)
  {61,0,0,0,-1,11}, // Table<<String,Value>>._grow()
  {62,0,0,0,7,46}, // Int32.hash_code()
  {63,1,11,9,7,48}, // Int32.or_smaller(Int32)
  {3,0,0,0,11,49}, // Int32.to_String()
  {64,0,0,0,32,52}, // Int32.to_digit()
  {0,0,0,0,-1,11}, // Array<<TableEntry<<String,Value>>>>.init_object()
  {1,0,0,0,0,1}, // Array<<TableEntry<<String,Value>>>>.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // Array<<TableEntry<<String,Value>>>>.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // Array<<TableEntry<<String,Value>>>>.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // Array<<TableEntry<<String,Value>>>>.type_name()
  {65,2,30,24,-1,74}, // Array<<TableEntry<<String,Value>>>>.zero(Int32,Int32)
  {0,0,0,0,-1,11}, // Array.init_object()
  {1,0,0,0,0,1}, // Array.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // Array.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // Array.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // Array.type_name()
  {65,2,30,24,-1,74}, // Array.zero(Int32,Int32)
  {0,0,0,0,10,1}, // TableEntry<<String,Value>>.init_object()
  {1,0,0,0,0,1}, // TableEntry<<String,Value>>.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // TableEntry<<String,Value>>.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // TableEntry<<String,Value>>.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // TableEntry<<String,Value>>.type_name()
  {1,3,32,26,10,97}, // TableEntry<<String,Value>>.init(String,Value,Int32)
  {0,0,0,0,-1,11}, // String.init_object()
  {1,0,0,0,0,1}, // String.init()
  {62,0,0,0,7,12}, // String.hash_code()
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // String.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // String.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // String.type_name()
  {66,1,10,9,11,6}, // String.after(Int32)
  {67,1,35,29,11,5}, // String.after_first(Character)
  {67,1,36,1,11,9}, // String.after_first(String)
  {68,1,35,29,11,5}, // String.after_last(Character)
  {69,1,10,9,11,6}, // String.before(Int32)
  {70,1,35,29,11,5}, // String.before_first(Character)
  {70,1,36,1,11,9}, // String.before_first(String)
  {71,1,35,29,11,5}, // String.before_last(Character)
  {72,1,35,29,21,16}, // String.begins_with(Character)
  {72,1,11,1,21,3}, // String.begins_with(String)
  {73,0,0,0,11,1}, // String.consolidated()
  {27,1,35,29,21,16}, // String.contains(Character)
  {27,1,37,1,21,3}, // String.contains(String)
  {74,2,38,30,21,17}, // String.contains_at(String,Int32)
  {75,1,35,29,21,16}, // String.ends_with(Character)
  {75,1,11,1,21,3}, // String.ends_with(String)
  {76,1,40,9,11,6}, // String.from(Int32)
  {76,2,41,24,11,2}, // String.from(Int32,Int32)
  {77,1,35,29,11,5}, // String.from_first(Character)
  {78,0,0,0,32,20}, // String.last()
  {79,2,43,32,11,21}, // String.left_justified(Int32,Character)
  {80,1,45,9,11,6}, // String.leftmost(Int32)
  {81,2,46,34,117,22}, // String.locate(Character,Int32?)
  {81,2,48,36,117,23}, // String.locate(String,Int32?)
  {82,2,50,34,117,22}, // String.locate_last(Character,Int32?)
  {83,1,0,29,11,5}, // String.operator+(Character)
  {83,1,0,9,11,6}, // String.operator+(Int32)
  {43,1,0,1,21,3}, // String.operator==(String)
  {83,1,0,38,11,7}, // String.operator+(Logical)
  {83,1,0,0,11,9}, // String.operator+(Object)
  {83,1,0,39,11,10}, // String.operator+(Real64)
  {83,1,0,1,11,9}, // String.operator+(String)
  {84,1,52,9,11,6}, // String.pluralized(Int32)
  {85,2,53,40,11,24}, // String.replacing(String,String)
  {86,1,45,9,11,6}, // String.rightmost(Int32)
  {87,1,55,29,27,5}, // String.split(Character)
  {88,1,45,9,11,6}, // String.times(Int32)
  {89,0,0,0,11,1}, // String.to_lowercase()
  {90,0,0,0,11,1}, // String.trimmed()
  {91,2,56,42,27,25}, // String.word_wrap(Int32,String)
  {91,3,58,44,13,26}, // String.word_wrap(Int32,StringBuilder,String)
  {0,0,0,0,12,1}, // (Function(TableEntry<<String,Value>>,TableEntry<<String,Value>>)->Logical).init_object()
  {1,0,0,0,0,1}, // (Function(TableEntry<<String,Value>>,TableEntry<<String,Value>>)->Logical).init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // (Function(TableEntry<<String,Value>>,TableEntry<<String,Value>>)->Logical).object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // (Function(TableEntry<<String,Value>>,TableEntry<<String,Value>>)->Logical).to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // (Function(TableEntry<<String,Value>>,TableEntry<<String,Value>>)->Logical).type_name()
  {92,2,61,47,21,98}, // (Function(TableEntry<<String,Value>>,TableEntry<<String,Value>>)->Logical).call(TableEntry<<String,Value>>,TableEntry<<String,Value>>)
  {0,0,0,0,13,1}, // StringBuilder.init_object()
  {1,0,0,0,13,1}, // StringBuilder.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // StringBuilder.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // StringBuilder.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // StringBuilder.type_name()
  {1,1,63,9,13,6}, // StringBuilder.init(Int32)
  {-1,-1,-1,-1,-1,1},
  {93,0,0,0,13,1}, // StringBuilder.clear()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {31,1,10,9,32,77}, // StringBuilder.get(Int32)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {81,1,35,29,117,78}, // StringBuilder.locate(Character)
  {-1,-1,-1,-1,-1,1},
  {94,0,0,0,21,19}, // StringBuilder.needs_indent()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {7,2,64,49,13,79}, // StringBuilder.print(Character,Logical)
  {7,1,0,9,13,6}, // StringBuilder.print(Int32)
  {7,1,0,38,13,7}, // StringBuilder.print(Logical)
  {7,1,0,51,13,8}, // StringBuilder.print(Int64)
  {7,1,0,0,13,9}, // StringBuilder.print(Object)
  {7,1,0,39,13,10}, // StringBuilder.print(Real64)
  {7,2,66,52,13,67}, // StringBuilder.print(Real64,Int32)
  {7,1,0,1,13,9}, // StringBuilder.print(String)
  {95,0,0,0,-1,11}, // StringBuilder.print_indent()
  {96,2,66,52,13,67}, // StringBuilder.print_to_work_bytes(Real64,Int32)
  {97,0,0,0,-1,11}, // StringBuilder.print_work_bytes()
  {8,0,0,0,13,1}, // StringBuilder.println()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {8,1,0,1,13,9}, // StringBuilder.println(String)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {98,1,68,9,13,6}, // StringBuilder.reserve(Int32)
  {99,0,0,0,-1,11}, // StringBuilder.round_off_work_bytes()
  {100,0,0,0,29,80}, // StringBuilder.scan_work_bytes()
  {0,0,0,0,14,1}, // Byte[].init_object()
  {1,0,0,0,14,1}, // Byte[].init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // Byte[].object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // Byte[].to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // Byte[].type_name()
  {1,1,63,9,14,6}, // Byte[].init(Int32)
  {101,1,0,54,14,4}, // Byte[].add(Byte)
  {102,0,0,0,7,12}, // Byte[].capacity()
  {93,0,0,0,14,1}, // Byte[].clear()
  {103,1,69,9,14,6}, // Byte[].ensure_capacity(Int32)
  {104,1,70,9,14,6}, // Byte[].expand_to_count(Int32)
  {105,1,10,9,14,6}, // Byte[].expand_to_include(Int32)
  {106,1,10,9,14,6}, // Byte[].discard_from(Int32)
  {107,2,71,55,14,100}, // Byte[].insert(Byte,Int32)
  {98,1,73,9,14,6}, // Byte[].reserve(Int32)
  {108,1,10,9,16,99}, // Byte[].remove_at(Int32)
  {109,0,0,0,16,102}, // Byte[].remove_last()
  {110,0,0,0,14,1}, // Byte[].reverse()
  {110,2,41,24,14,2}, // Byte[].reverse(Int32,Int32)
  {111,4,74,57,14,104}, // Byte[].shift(Int32,Int32?,Int32,Byte?)
  {0,0,0,0,15,1}, // GenericList.init_object()
  {1,0,0,0,0,1}, // GenericList.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // GenericList.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // GenericList.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // GenericList.type_name()
  {3,0,0,0,11,62}, // Byte.to_String()
  {0,0,0,0,-1,11}, // Array<<Byte>>.init_object()
  {1,0,0,0,0,1}, // Array<<Byte>>.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // Array<<Byte>>.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // Array<<Byte>>.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // Array<<Byte>>.type_name()
  {65,2,30,24,-1,74}, // Array<<Byte>>.zero(Int32,Int32)
  {0,0,0,0,18,1}, // StringBuilderPool.init_object()
  {1,0,0,0,0,1}, // StringBuilderPool.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // StringBuilderPool.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // StringBuilderPool.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // StringBuilderPool.type_name()
  {0,0,0,0,19,1}, // StringBuilder[].init_object()
  {1,0,0,0,19,1}, // StringBuilder[].init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // StringBuilder[].object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // StringBuilder[].to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // StringBuilder[].type_name()
  {1,1,63,9,19,6}, // StringBuilder[].init(Int32)
  {0,0,0,0,-1,11}, // Array<<StringBuilder>>.init_object()
  {1,0,0,0,0,1}, // Array<<StringBuilder>>.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // Array<<StringBuilder>>.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // Array<<StringBuilder>>.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // Array<<StringBuilder>>.type_name()
  {65,2,30,24,-1,74}, // Array<<StringBuilder>>.zero(Int32,Int32)
  {3,0,0,0,11,65}, // Logical.to_String()
  {0,0,0,0,22,1}, // (Function())[].init_object()
  {1,0,0,0,22,1}, // (Function())[].init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // (Function())[].object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // (Function())[].to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // (Function())[].type_name()
  {1,1,63,9,22,6}, // (Function())[].init(Int32)
  {101,1,0,8,22,9}, // (Function())[].add((Function()))
  {102,0,0,0,7,12}, // (Function())[].capacity()
  {98,1,73,9,22,6}, // (Function())[].reserve(Int32)
  {0,0,0,0,23,1}, // (Function()).init_object()
  {1,0,0,0,0,1}, // (Function()).init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // (Function()).object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // (Function()).to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // (Function()).type_name()
  {92,0,0,0,-1,11}, // (Function()).call()
  {0,0,0,0,-1,11}, // Array<<(Function())>>.init_object()
  {1,0,0,0,0,1}, // Array<<(Function())>>.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // Array<<(Function())>>.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // Array<<(Function())>>.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // Array<<(Function())>>.type_name()
  {65,2,30,24,-1,74}, // Array<<(Function())>>.zero(Int32,Int32)
  {0,0,0,0,25,1}, // Exception.init_object()
  {1,0,0,0,25,1}, // Exception.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // Exception.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // Exception.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // Exception.type_name()
  {1,1,78,1,25,9}, // Exception.init(String)
  {112,0,0,0,-1,11}, // Exception.display()
  {113,0,0,0,11,1}, // Exception.format()
  {0,0,0,0,26,1}, // StackTrace.init_object()
  {1,0,0,0,0,1}, // StackTrace.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // StackTrace.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // StackTrace.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // StackTrace.type_name()
  {1,1,79,9,26,6}, // StackTrace.init(Int32)
  {113,0,0,0,-1,11}, // StackTrace.format()
  {7,0,0,0,-1,11}, // StackTrace.print()
  {7,1,1,2,13,9}, // StackTrace.print(StringBuilder)
  {0,0,0,0,27,1}, // String[].init_object()
  {1,0,0,0,27,1}, // String[].init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // String[].object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // String[].to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // String[].type_name()
  {1,1,63,9,27,6}, // String[].init(Int32)
  {101,1,0,1,27,9}, // String[].add(String)
  {102,0,0,0,7,12}, // String[].capacity()
  {81,1,0,1,117,103}, // String[].locate(String)
  {98,1,73,9,27,6}, // String[].reserve(Int32)
  {44,1,0,1,11,9}, // String[].remove(String)
  {108,1,10,9,11,6}, // String[].remove_at(Int32)
  {114,1,55,1,11,9}, // String[].join(String)
  {115,1,80,61,27,9}, // String[].mapped<<String>>((Function(String)->String))
  {0,0,0,0,-1,11}, // Array<<String>>.init_object()
  {1,0,0,0,0,1}, // Array<<String>>.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // Array<<String>>.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // Array<<String>>.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // Array<<String>>.type_name()
  {65,2,30,24,-1,74}, // Array<<String>>.zero(Int32,Int32)
  {116,0,0,0,7,30}, // Real64.decimal_digit_count()
  {113,1,81,62,11,31}, // Real64.format(Int32?)
  {117,0,0,0,29,27}, // Real64.fractional_part()
  {118,0,0,0,21,34}, // Real64.is_infinite()
  {119,0,0,0,21,34}, // Real64.is_not_a_number()
  {3,0,0,0,11,33}, // Real64.to_String()
  {120,0,0,0,29,27}, // Real64.whole_part()
  {3,0,0,0,11,38}, // Real32.to_String()
  {121,3,82,63,13,43}, // Int64.print_in_power2_base(Int32,Int32,StringBuilder)
  {3,0,0,0,11,42}, // Int64.to_String()
  {122,1,85,9,11,45}, // Int64.to_hex_string(Int32)
  {123,2,86,15,21,57}, // Character.is_identifier(Logical,Logical)
  {124,0,0,0,21,56}, // Character.is_letter()
  {3,0,0,0,11,55}, // Character.to_String()
  {125,1,88,9,7,59}, // Character.to_number(Int32)
  {0,0,0,0,33,1}, // TypeInfo.init_object()
  {1,0,0,0,0,1}, // TypeInfo.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // TypeInfo.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // TypeInfo.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // TypeInfo.type_name()
  {1,2,89,42,33,25}, // TypeInfo.init(Int32,String)
  {126,2,91,24,-1,74}, // TypeInfo.add_global_property_info(Int32,Int32)
  {127,2,93,24,-1,74}, // TypeInfo.add_property_info(Int32,Int32)
  {128,1,95,9,-1,76}, // TypeInfo.add_method_info(Int32)
  {0,0,0,0,34,1}, // PropertyInfo[].init_object()
  {1,0,0,0,34,1}, // PropertyInfo[].init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // PropertyInfo[].object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // PropertyInfo[].to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // PropertyInfo[].type_name()
  {1,1,63,9,34,6}, // PropertyInfo[].init(Int32)
  {101,1,0,66,34,9}, // PropertyInfo[].add(PropertyInfo)
  {102,0,0,0,7,12}, // PropertyInfo[].capacity()
  {98,1,73,9,34,6}, // PropertyInfo[].reserve(Int32)
  {0,0,0,0,35,1}, // PropertyInfo.init_object()
  {1,0,0,0,0,1}, // PropertyInfo.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // PropertyInfo.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // PropertyInfo.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // PropertyInfo.type_name()
  {1,3,96,67,35,105}, // PropertyInfo.init(Int32,Int32,Int32)
  {129,0,0,0,11,1}, // PropertyInfo.name()
  {130,0,0,0,33,1}, // PropertyInfo.type()
  {0,0,0,0,-1,11}, // Array<<PropertyInfo>>.init_object()
  {1,0,0,0,0,1}, // Array<<PropertyInfo>>.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // Array<<PropertyInfo>>.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // Array<<PropertyInfo>>.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // Array<<PropertyInfo>>.type_name()
  {65,2,30,24,-1,74}, // Array<<PropertyInfo>>.zero(Int32,Int32)
  {0,0,0,0,37,1}, // MethodInfo[].init_object()
  {1,0,0,0,37,1}, // MethodInfo[].init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // MethodInfo[].object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // MethodInfo[].to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // MethodInfo[].type_name()
  {1,1,63,9,37,6}, // MethodInfo[].init(Int32)
  {101,1,0,70,37,9}, // MethodInfo[].add(MethodInfo)
  {102,0,0,0,7,12}, // MethodInfo[].capacity()
  {98,1,73,9,37,6}, // MethodInfo[].reserve(Int32)
  {0,0,0,0,38,1}, // MethodInfo.init_object()
  {1,0,0,0,0,1}, // MethodInfo.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // MethodInfo.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // MethodInfo.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // MethodInfo.type_name()
  {1,6,99,71,38,107}, // MethodInfo.init(Int32,Int32,Int32,Int32,Int32,Int32)
  {129,0,0,0,11,1}, // MethodInfo.name()
  {131,0,0,0,33,1}, // MethodInfo.return_type()
  {132,1,10,9,11,6}, // MethodInfo.parameter_name(Int32)
  {133,1,10,9,33,6}, // MethodInfo.parameter_type(Int32)
  {134,0,0,0,11,1}, // MethodInfo.signature()
  {0,0,0,0,39,1}, // Table<<Int32,String>>.init_object()
  {1,0,0,0,39,1}, // Table<<Int32,String>>.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // Table<<Int32,String>>.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // Table<<Int32,String>>.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // Table<<Int32,String>>.type_name()
  {1,1,27,9,39,6}, // Table<<Int32,String>>.init(Int32)
  {56,1,9,9,41,6}, // Table<<Int32,String>>.find(Int32)
  {31,1,9,9,11,6}, // Table<<Int32,String>>.get(Int32)
  {57,1,1,2,13,9}, // Table<<Int32,String>>.print_to(StringBuilder)
  {46,2,25,42,39,25}, // Table<<Int32,String>>.set(Int32,String)
  {58,1,29,77,-1,73}, // Table<<Int32,String>>._adjust_entry_order(TableEntry<<Int32,String>>)
  {59,1,29,77,-1,73}, // Table<<Int32,String>>._place_entry_in_order(TableEntry<<Int32,String>>)
  {60,1,29,77,-1,73}, // Table<<Int32,String>>._unlink(TableEntry<<Int32,String>>)
  {61,0,0,0,-1,11}, // Table<<Int32,String>>._grow()
  {0,0,0,0,-1,11}, // Array<<TableEntry<<Int32,String>>>>.init_object()
  {1,0,0,0,0,1}, // Array<<TableEntry<<Int32,String>>>>.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // Array<<TableEntry<<Int32,String>>>>.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // Array<<TableEntry<<Int32,String>>>>.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // Array<<TableEntry<<Int32,String>>>>.type_name()
  {65,2,30,24,-1,74}, // Array<<TableEntry<<Int32,String>>>>.zero(Int32,Int32)
  {0,0,0,0,41,1}, // TableEntry<<Int32,String>>.init_object()
  {1,0,0,0,0,1}, // TableEntry<<Int32,String>>.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // TableEntry<<Int32,String>>.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // TableEntry<<Int32,String>>.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // TableEntry<<Int32,String>>.type_name()
  {1,3,32,78,41,109}, // TableEntry<<Int32,String>>.init(Int32,String,Int32)
  {0,0,0,0,42,1}, // (Function(TableEntry<<Int32,String>>,TableEntry<<Int32,String>>)->Logical).init_object()
  {1,0,0,0,0,1}, // (Function(TableEntry<<Int32,String>>,TableEntry<<Int32,String>>)->Logical).init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // (Function(TableEntry<<Int32,String>>,TableEntry<<Int32,String>>)->Logical).object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // (Function(TableEntry<<Int32,String>>,TableEntry<<Int32,String>>)->Logical).to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // (Function(TableEntry<<Int32,String>>,TableEntry<<Int32,String>>)->Logical).type_name()
  {92,2,61,81,21,98}, // (Function(TableEntry<<Int32,String>>,TableEntry<<Int32,String>>)->Logical).call(TableEntry<<Int32,String>>,TableEntry<<Int32,String>>)
  {0,0,0,0,-1,11}, // Array<<MethodInfo>>.init_object()
  {1,0,0,0,0,1}, // Array<<MethodInfo>>.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // Array<<MethodInfo>>.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // Array<<MethodInfo>>.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // Array<<MethodInfo>>.type_name()
  {65,2,30,24,-1,74}, // Array<<MethodInfo>>.zero(Int32,Int32)
  {0,0,0,0,44,1}, // Character[].init_object()
  {1,0,0,0,44,1}, // Character[].init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // Character[].object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // Character[].to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // Character[].type_name()
  {1,1,63,9,44,6}, // Character[].init(Int32)
  {101,1,0,29,44,5}, // Character[].add(Character)
  {102,0,0,0,7,12}, // Character[].capacity()
  {106,1,10,9,44,6}, // Character[].discard_from(Int32)
  {98,1,73,9,44,6}, // Character[].reserve(Int32)
  {135,0,0,0,91,1}, // Character[].rewriter()
  {0,0,0,0,-1,11}, // Array<<Character>>.init_object()
  {1,0,0,0,0,1}, // Array<<Character>>.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // Array<<Character>>.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // Array<<Character>>.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // Array<<Character>>.type_name()
  {65,2,30,24,-1,74}, // Array<<Character>>.zero(Int32,Int32)
  {0,0,0,0,46,81}, // ValueList.init_object()
  {1,0,0,0,46,81}, // ValueList.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // ValueList.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,0}, // ValueList.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,0}, // ValueList.type_name()
  {101,1,0,3,46,83}, // ValueList.add(Value)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {27,1,0,1,21,82}, // ValueList.contains(String)
  {27,1,0,3,21,85}, // ValueList.contains(Value)
  {-1,-1,-1,-1,-1,1},
  {28,0,0,0,7,47}, // ValueList.count()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {30,1,9,1,4,84}, // ValueList.ensure_table(String)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {31,1,10,9,5,86}, // ValueList.get(Int32)
  {31,1,9,1,5,84}, // ValueList.get(String)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {32,0,0,0,21,64}, // ValueList.is_collection()
  {33,0,0,0,21,64}, // ValueList.is_complex()
  {34,0,0,0,21,64}, // ValueList.is_int32()
  {35,0,0,0,21,64}, // ValueList.is_int64()
  {-1,-1,-1,-1,-1,1},
  {36,0,0,0,21,64}, // ValueList.is_logical()
  {37,0,0,0,21,64}, // ValueList.is_null()
  {38,0,0,0,21,64}, // ValueList.is_non_null()
  {39,0,0,0,21,64}, // ValueList.is_number()
  {40,0,0,0,21,64}, // ValueList.is_object()
  {-1,-1,-1,-1,-1,1},
  {41,0,0,0,21,64}, // ValueList.is_string()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {43,1,11,3,21,85}, // ValueList.operator==(Value)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {43,1,11,1,21,82}, // ValueList.operator==(String)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {44,1,0,3,5,83}, // ValueList.remove(Value)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {45,3,12,10,21,88}, // ValueList.save(File,Logical,Logical)
  {-1,-1,-1,-1,-1,1},
  {46,2,25,13,5,89}, // ValueList.set(String,Value)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {47,0,0,0,16,61}, // ValueList.to_Byte()
  {48,0,0,0,32,54}, // ValueList.to_Character()
  {49,0,0,0,31,40}, // ValueList.to_Int64()
  {50,0,0,0,7,47}, // ValueList.to_Int32()
  {51,0,0,0,21,64}, // ValueList.to_Logical()
  {52,0,0,0,29,28}, // ValueList.to_Real64()
  {53,0,0,0,30,37}, // ValueList.to_Real32()
  {54,0,0,0,0,0}, // ValueList.to_Object()
  {-1,-1,-1,-1,-1,1},
  {55,2,17,15,11,90}, // ValueList.to_json(Logical,Logical)
  {55,3,19,17,13,91}, // ValueList.to_json(StringBuilder,Logical,Logical)
  {55,2,22,20,13,92}, // ValueList.to_json(StringBuilder,Int32)
  {-1,-1,-1,-1,-1,1},
  {1,1,63,9,46,86}, // ValueList.init(Int32)
  {0,0,0,0,47,1}, // Value[].init_object()
  {1,0,0,0,47,1}, // Value[].init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // Value[].object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // Value[].to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // Value[].type_name()
  {1,1,63,9,47,6}, // Value[].init(Int32)
  {101,1,0,3,47,72}, // Value[].add(Value)
  {102,0,0,0,7,12}, // Value[].capacity()
  {81,1,0,3,117,110}, // Value[].locate(Value)
  {98,1,73,9,47,6}, // Value[].reserve(Int32)
  {44,1,0,3,5,108}, // Value[].remove(Value)
  {108,1,10,9,5,94}, // Value[].remove_at(Int32)
  {0,0,0,0,-1,11}, // Array<<Value>>.init_object()
  {1,0,0,0,0,1}, // Array<<Value>>.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // Array<<Value>>.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // Array<<Value>>.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // Array<<Value>>.type_name()
  {65,2,30,24,-1,74}, // Array<<Value>>.zero(Int32,Int32)
  {0,0,0,0,49,81}, // ObjectValue.init_object()
  {1,0,0,0,0,1}, // ObjectValue.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // ObjectValue.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,0}, // ObjectValue.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,0}, // ObjectValue.type_name()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {27,1,24,1,21,82}, // ObjectValue.contains(String)
  {27,1,24,3,21,85}, // ObjectValue.contains(Value)
  {-1,-1,-1,-1,-1,1},
  {28,0,0,0,7,47}, // ObjectValue.count()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {30,1,9,1,4,84}, // ObjectValue.ensure_table(String)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {31,1,10,9,5,86}, // ObjectValue.get(Int32)
  {31,1,9,1,5,84}, // ObjectValue.get(String)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {32,0,0,0,21,64}, // ObjectValue.is_collection()
  {33,0,0,0,21,64}, // ObjectValue.is_complex()
  {34,0,0,0,21,64}, // ObjectValue.is_int32()
  {35,0,0,0,21,64}, // ObjectValue.is_int64()
  {-1,-1,-1,-1,-1,1},
  {36,0,0,0,21,64}, // ObjectValue.is_logical()
  {37,0,0,0,21,64}, // ObjectValue.is_null()
  {38,0,0,0,21,64}, // ObjectValue.is_non_null()
  {39,0,0,0,21,64}, // ObjectValue.is_number()
  {40,0,0,0,21,64}, // ObjectValue.is_object()
  {-1,-1,-1,-1,-1,1},
  {41,0,0,0,21,64}, // ObjectValue.is_string()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {43,1,11,3,21,85}, // ObjectValue.operator==(Value)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {43,1,11,1,21,82}, // ObjectValue.operator==(String)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {44,1,0,3,5,83}, // ObjectValue.remove(Value)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {45,3,12,10,21,88}, // ObjectValue.save(File,Logical,Logical)
  {-1,-1,-1,-1,-1,1},
  {46,2,25,13,5,89}, // ObjectValue.set(String,Value)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {47,0,0,0,16,61}, // ObjectValue.to_Byte()
  {48,0,0,0,32,54}, // ObjectValue.to_Character()
  {49,0,0,0,31,40}, // ObjectValue.to_Int64()
  {50,0,0,0,7,47}, // ObjectValue.to_Int32()
  {51,0,0,0,21,64}, // ObjectValue.to_Logical()
  {52,0,0,0,29,28}, // ObjectValue.to_Real64()
  {53,0,0,0,30,37}, // ObjectValue.to_Real32()
  {54,0,0,0,0,0}, // ObjectValue.to_Object()
  {-1,-1,-1,-1,-1,1},
  {55,2,17,15,11,90}, // ObjectValue.to_json(Logical,Logical)
  {55,3,19,17,13,91}, // ObjectValue.to_json(StringBuilder,Logical,Logical)
  {55,2,22,20,13,92}, // ObjectValue.to_json(StringBuilder,Int32)
  {-1,-1,-1,-1,-1,1},
  {1,1,105,0,49,84}, // ObjectValue.init(Object)
  {0,0,0,0,50,1}, // StringConsolidationTable.init_object()
  {1,0,0,0,52,1}, // StringConsolidationTable.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // StringConsolidationTable.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // StringConsolidationTable.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // StringConsolidationTable.type_name()
  {1,1,27,9,52,6}, // StringConsolidationTable.init(Int32)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {56,1,9,1,54,9}, // StringConsolidationTable.find(String)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {31,1,36,1,11,9}, // StringConsolidationTable.get(String)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {57,1,1,2,13,9}, // StringConsolidationTable.print_to(StringBuilder)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {46,2,25,40,52,24}, // StringConsolidationTable.set(String,String)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {58,1,29,83,-1,73}, // StringConsolidationTable._adjust_entry_order(TableEntry<<String,String>>)
  {59,1,29,83,-1,73}, // StringConsolidationTable._place_entry_in_order(TableEntry<<String,String>>)
  {60,1,29,83,-1,73}, // StringConsolidationTable._unlink(TableEntry<<String,String>>)
  {61,0,0,0,-1,11}, // StringConsolidationTable._grow()
  {0,0,0,0,51,1}, // StringTable<<String>>.init_object()
  {1,0,0,0,52,1}, // StringTable<<String>>.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // StringTable<<String>>.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // StringTable<<String>>.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // StringTable<<String>>.type_name()
  {1,1,27,9,52,6}, // StringTable<<String>>.init(Int32)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {56,1,9,1,54,9}, // StringTable<<String>>.find(String)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {31,1,9,1,11,9}, // StringTable<<String>>.get(String)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {57,1,1,2,13,9}, // StringTable<<String>>.print_to(StringBuilder)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {46,2,25,40,52,24}, // StringTable<<String>>.set(String,String)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {58,1,29,83,-1,73}, // StringTable<<String>>._adjust_entry_order(TableEntry<<String,String>>)
  {59,1,29,83,-1,73}, // StringTable<<String>>._place_entry_in_order(TableEntry<<String,String>>)
  {60,1,29,83,-1,73}, // StringTable<<String>>._unlink(TableEntry<<String,String>>)
  {61,0,0,0,-1,11}, // StringTable<<String>>._grow()
  {0,0,0,0,52,1}, // Table<<String,String>>.init_object()
  {1,0,0,0,52,1}, // Table<<String,String>>.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // Table<<String,String>>.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // Table<<String,String>>.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // Table<<String,String>>.type_name()
  {1,1,27,9,52,6}, // Table<<String,String>>.init(Int32)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {56,1,9,1,54,9}, // Table<<String,String>>.find(String)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {31,1,9,1,11,9}, // Table<<String,String>>.get(String)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {57,1,1,2,13,9}, // Table<<String,String>>.print_to(StringBuilder)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {46,2,25,40,52,24}, // Table<<String,String>>.set(String,String)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {58,1,29,83,-1,73}, // Table<<String,String>>._adjust_entry_order(TableEntry<<String,String>>)
  {59,1,29,83,-1,73}, // Table<<String,String>>._place_entry_in_order(TableEntry<<String,String>>)
  {60,1,29,83,-1,73}, // Table<<String,String>>._unlink(TableEntry<<String,String>>)
  {61,0,0,0,-1,11}, // Table<<String,String>>._grow()
  {0,0,0,0,-1,11}, // Array<<TableEntry<<String,String>>>>.init_object()
  {1,0,0,0,0,1}, // Array<<TableEntry<<String,String>>>>.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // Array<<TableEntry<<String,String>>>>.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // Array<<TableEntry<<String,String>>>>.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // Array<<TableEntry<<String,String>>>>.type_name()
  {65,2,30,24,-1,74}, // Array<<TableEntry<<String,String>>>>.zero(Int32,Int32)
  {0,0,0,0,54,1}, // TableEntry<<String,String>>.init_object()
  {1,0,0,0,0,1}, // TableEntry<<String,String>>.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // TableEntry<<String,String>>.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // TableEntry<<String,String>>.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // TableEntry<<String,String>>.type_name()
  {1,3,32,84,54,115}, // TableEntry<<String,String>>.init(String,String,Int32)
  {0,0,0,0,55,1}, // (Function(TableEntry<<String,String>>,TableEntry<<String,String>>)->Logical).init_object()
  {1,0,0,0,0,1}, // (Function(TableEntry<<String,String>>,TableEntry<<String,String>>)->Logical).init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // (Function(TableEntry<<String,String>>,TableEntry<<String,String>>)->Logical).object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // (Function(TableEntry<<String,String>>,TableEntry<<String,String>>)->Logical).to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // (Function(TableEntry<<String,String>>,TableEntry<<String,String>>)->Logical).type_name()
  {92,2,61,87,21,98}, // (Function(TableEntry<<String,String>>,TableEntry<<String,String>>)->Logical).call(TableEntry<<String,String>>,TableEntry<<String,String>>)
  {5,0,0,0,56,1}, // Reader<<Character>>.close()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {0,0,0,0,58,1}, // Console.init_object()
  {1,0,0,0,58,1}, // Console.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // Console.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // Console.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // Console.type_name()
  {5,0,0,0,59,1}, // Console.close()
  {136,0,0,0,21,19}, // Console.has_another()
  {137,0,0,0,32,20}, // Console.read()
  {6,0,0,0,58,1}, // Console.flush()
  {7,1,0,1,58,9}, // Console.print(String)
  {8,0,0,0,58,1}, // Console.println()
  {8,1,0,1,58,9}, // Console.println(String)
  {9,1,1,2,58,9}, // Console.write(StringBuilder)
  {138,0,0,0,-1,11}, // Console.fill_input_queue()
  {139,0,0,0,-1,11}, // Console.reset_input_mode()
  {140,0,0,0,7,12}, // Console.width()
  {5,0,0,0,59,1}, // PrintWriter<<output_buffer>>.close()
  {6,0,0,0,59,1}, // PrintWriter<<output_buffer>>.flush()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {8,1,0,1,59,9}, // PrintWriter<<output_buffer>>.println(String)
  {-1,-1,-1,-1,-1,1},
  {0,0,0,0,60,1}, // ConsoleErrorPrinter.init_object()
  {1,0,0,0,0,1}, // ConsoleErrorPrinter.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // ConsoleErrorPrinter.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // ConsoleErrorPrinter.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // ConsoleErrorPrinter.type_name()
  {5,0,0,0,60,1}, // ConsoleErrorPrinter.close()
  {6,0,0,0,60,1}, // ConsoleErrorPrinter.flush()
  {7,1,0,1,60,9}, // ConsoleErrorPrinter.print(String)
  {8,0,0,0,60,1}, // ConsoleErrorPrinter.println()
  {8,1,0,1,60,9}, // ConsoleErrorPrinter.println(String)
  {9,1,1,2,60,9}, // ConsoleErrorPrinter.write(StringBuilder)
  {0,0,0,0,61,1}, // PrimitiveWorkBuffer.init_object()
  {1,0,0,0,13,1}, // PrimitiveWorkBuffer.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // PrimitiveWorkBuffer.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // PrimitiveWorkBuffer.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // PrimitiveWorkBuffer.type_name()
  {1,1,63,9,13,6}, // PrimitiveWorkBuffer.init(Int32)
  {-1,-1,-1,-1,-1,1},
  {93,0,0,0,13,1}, // PrimitiveWorkBuffer.clear()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {31,1,10,9,32,77}, // PrimitiveWorkBuffer.get(Int32)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {81,1,35,29,117,78}, // PrimitiveWorkBuffer.locate(Character)
  {-1,-1,-1,-1,-1,1},
  {94,0,0,0,21,19}, // PrimitiveWorkBuffer.needs_indent()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {7,2,64,49,13,79}, // PrimitiveWorkBuffer.print(Character,Logical)
  {7,1,0,9,13,6}, // PrimitiveWorkBuffer.print(Int32)
  {7,1,0,38,13,7}, // PrimitiveWorkBuffer.print(Logical)
  {7,1,0,51,13,8}, // PrimitiveWorkBuffer.print(Int64)
  {7,1,0,0,13,9}, // PrimitiveWorkBuffer.print(Object)
  {7,1,0,39,13,10}, // PrimitiveWorkBuffer.print(Real64)
  {7,2,66,52,13,67}, // PrimitiveWorkBuffer.print(Real64,Int32)
  {7,1,0,1,13,9}, // PrimitiveWorkBuffer.print(String)
  {95,0,0,0,-1,11}, // PrimitiveWorkBuffer.print_indent()
  {96,2,66,52,13,67}, // PrimitiveWorkBuffer.print_to_work_bytes(Real64,Int32)
  {97,0,0,0,-1,11}, // PrimitiveWorkBuffer.print_work_bytes()
  {8,0,0,0,13,1}, // PrimitiveWorkBuffer.println()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {8,1,0,1,13,9}, // PrimitiveWorkBuffer.println(String)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {98,1,68,9,13,6}, // PrimitiveWorkBuffer.reserve(Int32)
  {99,0,0,0,-1,11}, // PrimitiveWorkBuffer.round_off_work_bytes()
  {100,0,0,0,29,80}, // PrimitiveWorkBuffer.scan_work_bytes()
  {0,0,0,0,62,1}, // Math.init_object()
  {1,0,0,0,0,1}, // Math.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // Math.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // Math.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // Math.type_name()
  {0,0,0,0,63,1}, // Boxed.init_object()
  {1,0,0,0,0,1}, // Boxed.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // Boxed.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // Boxed.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // Boxed.type_name()
  {0,0,0,0,64,1}, // Function_221.init_object()
  {1,0,0,0,0,1}, // Function_221.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // Function_221.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // Function_221.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // Function_221.type_name()
  {92,0,0,0,-1,11}, // Function_221.call()
  {0,0,0,0,65,1}, // System.init_object()
  {1,0,0,0,0,1}, // System.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // System.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // System.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // System.type_name()
  {0,0,0,0,66,1}, // Error.init_object()
  {1,0,0,0,25,1}, // Error.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // Error.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // Error.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // Error.type_name()
  {1,1,78,1,25,9}, // Error.init(String)
  {112,0,0,0,-1,11}, // Error.display()
  {113,0,0,0,11,1}, // Error.format()
  {141,0,0,0,66,1}, // Error._throw()
  {0,0,0,0,67,1}, // File.init_object()
  {1,0,0,0,0,1}, // File.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // File.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // File.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // File.type_name()
  {1,1,106,1,67,9}, // File.init(String)
  {142,0,0,0,11,1}, // File.filename()
  {0,0,0,0,68,1}, // LineReader.init_object()
  {1,0,0,0,0,1}, // LineReader.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // LineReader.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // LineReader.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // LineReader.type_name()
  {5,0,0,0,68,1}, // LineReader.close()
  {136,0,0,0,21,19}, // LineReader.has_another()
  {137,0,0,0,11,1}, // LineReader.read()
  {1,1,107,89,68,9}, // LineReader.init(Reader<<Character>>)
  {1,1,108,90,68,9}, // LineReader.init(Reader<<Byte>>)
  {1,1,6,6,68,9}, // LineReader.init(File)
  {143,0,0,0,11,1}, // LineReader.prepare_next()
  {5,0,0,0,69,1}, // Reader<<Byte>>.close()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {0,0,0,0,70,1}, // FileWriter.init_object()
  {1,0,0,0,0,1}, // FileWriter.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // FileWriter.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // FileWriter.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // FileWriter.type_name()
  {5,0,0,0,70,1}, // FileWriter.close()
  {6,0,0,0,70,1}, // FileWriter.flush()
  {9,1,35,54,70,4}, // FileWriter.write(Byte)
  {9,1,109,91,70,9}, // FileWriter.write(Byte[])
  {1,2,110,92,70,122}, // FileWriter.init(String,Logical)
  {144,0,0,0,-1,11}, // FileWriter.on_cleanup()
  {145,2,112,92,21,18}, // FileWriter.open(String,Logical)
  {9,1,114,1,70,9}, // FileWriter.write(String)
  {5,0,0,0,71,1}, // Writer<<Byte>>.close()
  {6,0,0,0,71,1}, // Writer<<Byte>>.flush()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {9,1,28,91,71,9}, // Writer<<Byte>>.write(Byte[])
  {0,0,0,0,72,1}, // Scanner.init_object()
  {1,0,0,0,0,1}, // Scanner.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // Scanner.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // Scanner.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // Scanner.type_name()
  {5,0,0,0,72,1}, // Scanner.close()
  {136,0,0,0,21,19}, // Scanner.has_another()
  {146,0,0,0,32,20}, // Scanner.peek()
  {137,0,0,0,32,20}, // Scanner.read()
  {1,3,115,94,72,123}, // Scanner.init(String,Int32,Logical)
  {147,1,35,29,21,16}, // Scanner.consume(Character)
  {148,0,0,0,21,19}, // Scanner.consume_eols()
  {149,0,0,0,21,19}, // Scanner.consume_spaces()
  {150,0,0,0,72,1}, // Scanner.convert_crlf_to_newline()
  {0,0,0,0,73,1}, // JSONParser.init_object()
  {1,0,0,0,0,1}, // JSONParser.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // JSONParser.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // JSONParser.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // JSONParser.type_name()
  {1,1,118,1,73,9}, // JSONParser.init(String)
  {1,1,119,97,73,9}, // JSONParser.init(Scanner)
  {147,1,35,29,21,16}, // JSONParser.consume(Character)
  {136,0,0,0,21,19}, // JSONParser.has_another()
  {151,1,35,29,21,16}, // JSONParser.next_is(Character)
  {152,0,0,0,5,14}, // JSONParser.parse_value()
  {153,2,120,98,5,124}, // JSONParser.parse_table(Character,Character)
  {154,2,120,98,5,124}, // JSONParser.parse_list(Character,Character)
  {155,0,0,0,11,1}, // JSONParser.parse_string()
  {156,0,0,0,32,20}, // JSONParser.parse_hex_quad()
  {157,0,0,0,11,1}, // JSONParser.parse_identifier()
  {146,0,0,0,32,20}, // JSONParser.peek()
  {158,0,0,0,21,19}, // JSONParser.next_is_identifier()
  {159,0,0,0,5,14}, // JSONParser.parse_number()
  {137,0,0,0,32,20}, // JSONParser.read()
  {149,0,0,0,-1,11}, // JSONParser.consume_spaces()
  {160,0,0,0,-1,11}, // JSONParser.consume_spaces_and_eols()
  {0,0,0,0,74,1}, // JSON.init_object()
  {1,0,0,0,0,1}, // JSON.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // JSON.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // JSON.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // JSON.type_name()
  {0,0,0,0,75,1}, // (Function(String)->String).init_object()
  {1,0,0,0,0,1}, // (Function(String)->String).init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // (Function(String)->String).object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // (Function(String)->String).to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // (Function(String)->String).type_name()
  {92,1,122,1,11,9}, // (Function(String)->String).call(String)
  {0,0,0,0,76,1}, // Function_281.init_object()
  {1,0,0,0,0,1}, // Function_281.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // Function_281.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // Function_281.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // Function_281.type_name()
  {92,1,2,1,11,9}, // Function_281.call(String)
  {0,0,0,0,77,1}, // Runtime.init_object()
  {1,0,0,0,0,1}, // Runtime.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // Runtime.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // Runtime.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // Runtime.type_name()
  {0,0,0,0,78,1}, // WeakReference.init_object()
  {1,0,0,0,0,1}, // WeakReference.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // WeakReference.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // WeakReference.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // WeakReference.type_name()
  {144,0,0,0,-1,11}, // WeakReference.on_cleanup()
  {0,0,0,0,79,1}, // PrintWriterAdapter.init_object()
  {1,0,0,0,0,1}, // PrintWriterAdapter.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // PrintWriterAdapter.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // PrintWriterAdapter.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // PrintWriterAdapter.type_name()
  {5,0,0,0,79,1}, // PrintWriterAdapter.close()
  {6,0,0,0,79,1}, // PrintWriterAdapter.flush()
  {7,1,0,1,79,9}, // PrintWriterAdapter.print(String)
  {8,0,0,0,79,1}, // PrintWriterAdapter.println()
  {8,1,0,1,79,9}, // PrintWriterAdapter.println(String)
  {9,1,123,2,79,9}, // PrintWriterAdapter.write(StringBuilder)
  {1,1,124,100,79,9}, // PrintWriterAdapter.init(Writer<<Byte>>)
  {5,0,0,0,80,1}, // PrintWriter<<buffer>>.close()
  {6,0,0,0,80,1}, // PrintWriter<<buffer>>.flush()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {8,1,0,1,80,9}, // PrintWriter<<buffer>>.println(String)
  {-1,-1,-1,-1,-1,1},
  {0,0,0,0,81,81}, // LogicalValue.init_object()
  {1,0,0,0,0,1}, // LogicalValue.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // LogicalValue.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,0}, // LogicalValue.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,0}, // LogicalValue.type_name()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {27,1,24,1,21,82}, // LogicalValue.contains(String)
  {27,1,24,3,21,85}, // LogicalValue.contains(Value)
  {-1,-1,-1,-1,-1,1},
  {28,0,0,0,7,47}, // LogicalValue.count()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {30,1,9,1,4,84}, // LogicalValue.ensure_table(String)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {31,1,10,9,5,86}, // LogicalValue.get(Int32)
  {31,1,9,1,5,84}, // LogicalValue.get(String)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {32,0,0,0,21,64}, // LogicalValue.is_collection()
  {33,0,0,0,21,64}, // LogicalValue.is_complex()
  {34,0,0,0,21,64}, // LogicalValue.is_int32()
  {35,0,0,0,21,64}, // LogicalValue.is_int64()
  {-1,-1,-1,-1,-1,1},
  {36,0,0,0,21,64}, // LogicalValue.is_logical()
  {37,0,0,0,21,64}, // LogicalValue.is_null()
  {38,0,0,0,21,64}, // LogicalValue.is_non_null()
  {39,0,0,0,21,64}, // LogicalValue.is_number()
  {40,0,0,0,21,64}, // LogicalValue.is_object()
  {-1,-1,-1,-1,-1,1},
  {41,0,0,0,21,64}, // LogicalValue.is_string()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {43,1,11,3,21,85}, // LogicalValue.operator==(Value)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {43,1,11,1,21,82}, // LogicalValue.operator==(String)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {44,1,0,3,5,83}, // LogicalValue.remove(Value)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {45,3,12,10,21,88}, // LogicalValue.save(File,Logical,Logical)
  {-1,-1,-1,-1,-1,1},
  {46,2,25,13,5,89}, // LogicalValue.set(String,Value)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {47,0,0,0,16,61}, // LogicalValue.to_Byte()
  {48,0,0,0,32,54}, // LogicalValue.to_Character()
  {49,0,0,0,31,40}, // LogicalValue.to_Int64()
  {50,0,0,0,7,47}, // LogicalValue.to_Int32()
  {51,0,0,0,21,64}, // LogicalValue.to_Logical()
  {52,0,0,0,29,28}, // LogicalValue.to_Real64()
  {53,0,0,0,30,37}, // LogicalValue.to_Real32()
  {54,0,0,0,0,0}, // LogicalValue.to_Object()
  {-1,-1,-1,-1,-1,1},
  {55,2,17,15,11,90}, // LogicalValue.to_json(Logical,Logical)
  {55,3,19,17,13,91}, // LogicalValue.to_json(StringBuilder,Logical,Logical)
  {55,2,22,20,13,92}, // LogicalValue.to_json(StringBuilder,Int32)
  {-1,-1,-1,-1,-1,1},
  {1,1,125,38,81,93}, // LogicalValue.init(Logical)
  {0,0,0,0,82,81}, // Int32Value.init_object()
  {1,0,0,0,0,1}, // Int32Value.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // Int32Value.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,0}, // Int32Value.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,0}, // Int32Value.type_name()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {27,1,24,1,21,82}, // Int32Value.contains(String)
  {27,1,24,3,21,85}, // Int32Value.contains(Value)
  {-1,-1,-1,-1,-1,1},
  {28,0,0,0,7,47}, // Int32Value.count()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {30,1,9,1,4,84}, // Int32Value.ensure_table(String)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {31,1,10,9,5,86}, // Int32Value.get(Int32)
  {31,1,9,1,5,84}, // Int32Value.get(String)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {32,0,0,0,21,64}, // Int32Value.is_collection()
  {33,0,0,0,21,64}, // Int32Value.is_complex()
  {34,0,0,0,21,64}, // Int32Value.is_int32()
  {35,0,0,0,21,64}, // Int32Value.is_int64()
  {-1,-1,-1,-1,-1,1},
  {36,0,0,0,21,64}, // Int32Value.is_logical()
  {37,0,0,0,21,64}, // Int32Value.is_null()
  {38,0,0,0,21,64}, // Int32Value.is_non_null()
  {39,0,0,0,21,64}, // Int32Value.is_number()
  {40,0,0,0,21,64}, // Int32Value.is_object()
  {-1,-1,-1,-1,-1,1},
  {41,0,0,0,21,64}, // Int32Value.is_string()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {43,1,11,3,21,85}, // Int32Value.operator==(Value)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {43,1,11,1,21,82}, // Int32Value.operator==(String)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {44,1,0,3,5,83}, // Int32Value.remove(Value)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {45,3,12,10,21,88}, // Int32Value.save(File,Logical,Logical)
  {-1,-1,-1,-1,-1,1},
  {46,2,25,13,5,89}, // Int32Value.set(String,Value)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {47,0,0,0,16,61}, // Int32Value.to_Byte()
  {48,0,0,0,32,54}, // Int32Value.to_Character()
  {49,0,0,0,31,40}, // Int32Value.to_Int64()
  {50,0,0,0,7,47}, // Int32Value.to_Int32()
  {51,0,0,0,21,64}, // Int32Value.to_Logical()
  {52,0,0,0,29,28}, // Int32Value.to_Real64()
  {53,0,0,0,30,37}, // Int32Value.to_Real32()
  {54,0,0,0,0,0}, // Int32Value.to_Object()
  {-1,-1,-1,-1,-1,1},
  {55,2,17,15,11,90}, // Int32Value.to_json(Logical,Logical)
  {55,3,19,17,13,91}, // Int32Value.to_json(StringBuilder,Logical,Logical)
  {55,2,22,20,13,92}, // Int32Value.to_json(StringBuilder,Int32)
  {-1,-1,-1,-1,-1,1},
  {1,1,126,9,82,86}, // Int32Value.init(Int32)
  {0,0,0,0,83,81}, // Int64Value.init_object()
  {1,0,0,0,0,1}, // Int64Value.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // Int64Value.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,0}, // Int64Value.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,0}, // Int64Value.type_name()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {27,1,24,1,21,82}, // Int64Value.contains(String)
  {27,1,24,3,21,85}, // Int64Value.contains(Value)
  {-1,-1,-1,-1,-1,1},
  {28,0,0,0,7,47}, // Int64Value.count()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {30,1,9,1,4,84}, // Int64Value.ensure_table(String)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {31,1,10,9,5,86}, // Int64Value.get(Int32)
  {31,1,9,1,5,84}, // Int64Value.get(String)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {32,0,0,0,21,64}, // Int64Value.is_collection()
  {33,0,0,0,21,64}, // Int64Value.is_complex()
  {34,0,0,0,21,64}, // Int64Value.is_int32()
  {35,0,0,0,21,64}, // Int64Value.is_int64()
  {-1,-1,-1,-1,-1,1},
  {36,0,0,0,21,64}, // Int64Value.is_logical()
  {37,0,0,0,21,64}, // Int64Value.is_null()
  {38,0,0,0,21,64}, // Int64Value.is_non_null()
  {39,0,0,0,21,64}, // Int64Value.is_number()
  {40,0,0,0,21,64}, // Int64Value.is_object()
  {-1,-1,-1,-1,-1,1},
  {41,0,0,0,21,64}, // Int64Value.is_string()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {43,1,11,3,21,85}, // Int64Value.operator==(Value)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {43,1,11,1,21,82}, // Int64Value.operator==(String)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {44,1,0,3,5,83}, // Int64Value.remove(Value)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {45,3,12,10,21,88}, // Int64Value.save(File,Logical,Logical)
  {-1,-1,-1,-1,-1,1},
  {46,2,25,13,5,89}, // Int64Value.set(String,Value)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {47,0,0,0,16,61}, // Int64Value.to_Byte()
  {48,0,0,0,32,54}, // Int64Value.to_Character()
  {49,0,0,0,31,40}, // Int64Value.to_Int64()
  {50,0,0,0,7,47}, // Int64Value.to_Int32()
  {51,0,0,0,21,64}, // Int64Value.to_Logical()
  {52,0,0,0,29,28}, // Int64Value.to_Real64()
  {53,0,0,0,30,37}, // Int64Value.to_Real32()
  {54,0,0,0,0,0}, // Int64Value.to_Object()
  {-1,-1,-1,-1,-1,1},
  {55,2,17,15,11,90}, // Int64Value.to_json(Logical,Logical)
  {55,3,19,17,13,91}, // Int64Value.to_json(StringBuilder,Logical,Logical)
  {55,2,22,20,13,92}, // Int64Value.to_json(StringBuilder,Int32)
  {-1,-1,-1,-1,-1,1},
  {1,1,127,51,83,129}, // Int64Value.init(Int64)
  {0,0,0,0,84,81}, // Real64Value.init_object()
  {1,0,0,0,0,1}, // Real64Value.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // Real64Value.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,0}, // Real64Value.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,0}, // Real64Value.type_name()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {27,1,24,1,21,82}, // Real64Value.contains(String)
  {27,1,24,3,21,85}, // Real64Value.contains(Value)
  {-1,-1,-1,-1,-1,1},
  {28,0,0,0,7,47}, // Real64Value.count()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {30,1,9,1,4,84}, // Real64Value.ensure_table(String)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {31,1,10,9,5,86}, // Real64Value.get(Int32)
  {31,1,9,1,5,84}, // Real64Value.get(String)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {32,0,0,0,21,64}, // Real64Value.is_collection()
  {33,0,0,0,21,64}, // Real64Value.is_complex()
  {34,0,0,0,21,64}, // Real64Value.is_int32()
  {35,0,0,0,21,64}, // Real64Value.is_int64()
  {-1,-1,-1,-1,-1,1},
  {36,0,0,0,21,64}, // Real64Value.is_logical()
  {37,0,0,0,21,64}, // Real64Value.is_null()
  {38,0,0,0,21,64}, // Real64Value.is_non_null()
  {39,0,0,0,21,64}, // Real64Value.is_number()
  {40,0,0,0,21,64}, // Real64Value.is_object()
  {-1,-1,-1,-1,-1,1},
  {41,0,0,0,21,64}, // Real64Value.is_string()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {43,1,11,3,21,85}, // Real64Value.operator==(Value)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {43,1,11,1,21,82}, // Real64Value.operator==(String)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {44,1,0,3,5,83}, // Real64Value.remove(Value)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {45,3,12,10,21,88}, // Real64Value.save(File,Logical,Logical)
  {-1,-1,-1,-1,-1,1},
  {46,2,25,13,5,89}, // Real64Value.set(String,Value)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {47,0,0,0,16,61}, // Real64Value.to_Byte()
  {48,0,0,0,32,54}, // Real64Value.to_Character()
  {49,0,0,0,31,40}, // Real64Value.to_Int64()
  {50,0,0,0,7,47}, // Real64Value.to_Int32()
  {51,0,0,0,21,64}, // Real64Value.to_Logical()
  {52,0,0,0,29,28}, // Real64Value.to_Real64()
  {53,0,0,0,30,37}, // Real64Value.to_Real32()
  {54,0,0,0,0,0}, // Real64Value.to_Object()
  {-1,-1,-1,-1,-1,1},
  {55,2,17,15,11,90}, // Real64Value.to_json(Logical,Logical)
  {55,3,19,17,13,91}, // Real64Value.to_json(StringBuilder,Logical,Logical)
  {55,2,22,20,13,92}, // Real64Value.to_json(StringBuilder,Int32)
  {-1,-1,-1,-1,-1,1},
  {1,1,128,39,84,87}, // Real64Value.init(Real64)
  {0,0,0,0,85,81}, // NullValue.init_object()
  {1,0,0,0,0,1}, // NullValue.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // NullValue.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,0}, // NullValue.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,0}, // NullValue.type_name()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {27,1,24,1,21,82}, // NullValue.contains(String)
  {27,1,24,3,21,85}, // NullValue.contains(Value)
  {-1,-1,-1,-1,-1,1},
  {28,0,0,0,7,47}, // NullValue.count()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {30,1,9,1,4,84}, // NullValue.ensure_table(String)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {31,1,10,9,5,86}, // NullValue.get(Int32)
  {31,1,9,1,5,84}, // NullValue.get(String)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {32,0,0,0,21,64}, // NullValue.is_collection()
  {33,0,0,0,21,64}, // NullValue.is_complex()
  {34,0,0,0,21,64}, // NullValue.is_int32()
  {35,0,0,0,21,64}, // NullValue.is_int64()
  {-1,-1,-1,-1,-1,1},
  {36,0,0,0,21,64}, // NullValue.is_logical()
  {37,0,0,0,21,64}, // NullValue.is_null()
  {38,0,0,0,21,64}, // NullValue.is_non_null()
  {39,0,0,0,21,64}, // NullValue.is_number()
  {40,0,0,0,21,64}, // NullValue.is_object()
  {-1,-1,-1,-1,-1,1},
  {41,0,0,0,21,64}, // NullValue.is_string()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {43,1,11,3,21,85}, // NullValue.operator==(Value)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {43,1,11,1,21,82}, // NullValue.operator==(String)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {44,1,0,3,5,83}, // NullValue.remove(Value)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {45,3,12,10,21,88}, // NullValue.save(File,Logical,Logical)
  {-1,-1,-1,-1,-1,1},
  {46,2,25,13,5,89}, // NullValue.set(String,Value)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {47,0,0,0,16,61}, // NullValue.to_Byte()
  {48,0,0,0,32,54}, // NullValue.to_Character()
  {49,0,0,0,31,40}, // NullValue.to_Int64()
  {50,0,0,0,7,47}, // NullValue.to_Int32()
  {51,0,0,0,21,64}, // NullValue.to_Logical()
  {52,0,0,0,29,28}, // NullValue.to_Real64()
  {53,0,0,0,30,37}, // NullValue.to_Real32()
  {54,0,0,0,0,0}, // NullValue.to_Object()
  {-1,-1,-1,-1,-1,1},
  {55,2,17,15,11,90}, // NullValue.to_json(Logical,Logical)
  {55,3,19,17,13,91}, // NullValue.to_json(StringBuilder,Logical,Logical)
  {55,2,22,20,13,92}, // NullValue.to_json(StringBuilder,Int32)
  {0,0,0,0,86,81}, // StringValue.init_object()
  {1,0,0,0,0,1}, // StringValue.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // StringValue.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,0}, // StringValue.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,0}, // StringValue.type_name()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {27,1,24,1,21,82}, // StringValue.contains(String)
  {27,1,24,3,21,85}, // StringValue.contains(Value)
  {-1,-1,-1,-1,-1,1},
  {28,0,0,0,7,47}, // StringValue.count()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {30,1,9,1,4,84}, // StringValue.ensure_table(String)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {31,1,10,9,5,86}, // StringValue.get(Int32)
  {31,1,9,1,5,84}, // StringValue.get(String)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {32,0,0,0,21,64}, // StringValue.is_collection()
  {33,0,0,0,21,64}, // StringValue.is_complex()
  {34,0,0,0,21,64}, // StringValue.is_int32()
  {35,0,0,0,21,64}, // StringValue.is_int64()
  {-1,-1,-1,-1,-1,1},
  {36,0,0,0,21,64}, // StringValue.is_logical()
  {37,0,0,0,21,64}, // StringValue.is_null()
  {38,0,0,0,21,64}, // StringValue.is_non_null()
  {39,0,0,0,21,64}, // StringValue.is_number()
  {40,0,0,0,21,64}, // StringValue.is_object()
  {-1,-1,-1,-1,-1,1},
  {41,0,0,0,21,64}, // StringValue.is_string()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {43,1,11,3,21,85}, // StringValue.operator==(Value)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {43,1,11,1,21,82}, // StringValue.operator==(String)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {44,1,0,3,5,83}, // StringValue.remove(Value)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {45,3,12,10,21,88}, // StringValue.save(File,Logical,Logical)
  {-1,-1,-1,-1,-1,1},
  {46,2,25,13,5,89}, // StringValue.set(String,Value)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {47,0,0,0,16,61}, // StringValue.to_Byte()
  {48,0,0,0,32,54}, // StringValue.to_Character()
  {49,0,0,0,31,40}, // StringValue.to_Int64()
  {50,0,0,0,7,47}, // StringValue.to_Int32()
  {51,0,0,0,21,64}, // StringValue.to_Logical()
  {52,0,0,0,29,28}, // StringValue.to_Real64()
  {53,0,0,0,30,37}, // StringValue.to_Real32()
  {54,0,0,0,0,0}, // StringValue.to_Object()
  {-1,-1,-1,-1,-1,1},
  {55,2,17,15,11,90}, // StringValue.to_json(Logical,Logical)
  {55,3,19,17,13,91}, // StringValue.to_json(StringBuilder,Logical,Logical)
  {55,2,22,20,13,92}, // StringValue.to_json(StringBuilder,Int32)
  {-1,-1,-1,-1,-1,1},
  {1,1,129,1,86,84}, // StringValue.init(String)
  {0,0,0,0,87,81}, // UndefinedValue.init_object()
  {1,0,0,0,0,1}, // UndefinedValue.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // UndefinedValue.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,0}, // UndefinedValue.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,0}, // UndefinedValue.type_name()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {27,1,24,1,21,82}, // UndefinedValue.contains(String)
  {27,1,24,3,21,85}, // UndefinedValue.contains(Value)
  {-1,-1,-1,-1,-1,1},
  {28,0,0,0,7,47}, // UndefinedValue.count()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {30,1,9,1,4,84}, // UndefinedValue.ensure_table(String)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {31,1,10,9,5,86}, // UndefinedValue.get(Int32)
  {31,1,9,1,5,84}, // UndefinedValue.get(String)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {32,0,0,0,21,64}, // UndefinedValue.is_collection()
  {33,0,0,0,21,64}, // UndefinedValue.is_complex()
  {34,0,0,0,21,64}, // UndefinedValue.is_int32()
  {35,0,0,0,21,64}, // UndefinedValue.is_int64()
  {-1,-1,-1,-1,-1,1},
  {36,0,0,0,21,64}, // UndefinedValue.is_logical()
  {37,0,0,0,21,64}, // UndefinedValue.is_null()
  {38,0,0,0,21,64}, // UndefinedValue.is_non_null()
  {39,0,0,0,21,64}, // UndefinedValue.is_number()
  {40,0,0,0,21,64}, // UndefinedValue.is_object()
  {-1,-1,-1,-1,-1,1},
  {41,0,0,0,21,64}, // UndefinedValue.is_string()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {43,1,11,3,21,85}, // UndefinedValue.operator==(Value)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {43,1,11,1,21,82}, // UndefinedValue.operator==(String)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {44,1,0,3,5,83}, // UndefinedValue.remove(Value)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {45,3,12,10,21,88}, // UndefinedValue.save(File,Logical,Logical)
  {-1,-1,-1,-1,-1,1},
  {46,2,25,13,5,89}, // UndefinedValue.set(String,Value)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {47,0,0,0,16,61}, // UndefinedValue.to_Byte()
  {48,0,0,0,32,54}, // UndefinedValue.to_Character()
  {49,0,0,0,31,40}, // UndefinedValue.to_Int64()
  {50,0,0,0,7,47}, // UndefinedValue.to_Int32()
  {51,0,0,0,21,64}, // UndefinedValue.to_Logical()
  {52,0,0,0,29,28}, // UndefinedValue.to_Real64()
  {53,0,0,0,30,37}, // UndefinedValue.to_Real32()
  {54,0,0,0,0,0}, // UndefinedValue.to_Object()
  {-1,-1,-1,-1,-1,1},
  {55,2,17,15,11,90}, // UndefinedValue.to_json(Logical,Logical)
  {55,3,19,17,13,91}, // UndefinedValue.to_json(StringBuilder,Logical,Logical)
  {55,2,22,20,13,92}, // UndefinedValue.to_json(StringBuilder,Int32)
  {0,0,0,0,88,1}, // OutOfBoundsError.init_object()
  {1,0,0,0,25,1}, // OutOfBoundsError.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // OutOfBoundsError.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // OutOfBoundsError.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // OutOfBoundsError.type_name()
  {1,1,130,1,88,9}, // OutOfBoundsError.init(String)
  {112,0,0,0,-1,11}, // OutOfBoundsError.display()
  {113,0,0,0,11,1}, // OutOfBoundsError.format()
  {141,0,0,0,88,1}, // OutOfBoundsError._throw()
  {1,2,131,24,88,2}, // OutOfBoundsError.init(Int32,Int32)
  {0,0,0,0,89,1}, // TableEntry<<String,TypeInfo>>.init_object()
  {1,0,0,0,0,1}, // TableEntry<<String,TypeInfo>>.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // TableEntry<<String,TypeInfo>>.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // TableEntry<<String,TypeInfo>>.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // TableEntry<<String,TypeInfo>>.type_name()
  {0,0,0,0,90,1}, // RequirementError.init_object()
  {1,0,0,0,25,1}, // RequirementError.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // RequirementError.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // RequirementError.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // RequirementError.type_name()
  {1,1,133,1,90,9}, // RequirementError.init(String)
  {112,0,0,0,-1,11}, // RequirementError.display()
  {113,0,0,0,11,1}, // RequirementError.format()
  {141,0,0,0,90,1}, // RequirementError._throw()
  {0,0,0,0,91,1}, // ListRewriter<<Character>>.init_object()
  {1,0,0,0,0,1}, // ListRewriter<<Character>>.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // ListRewriter<<Character>>.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // ListRewriter<<Character>>.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // ListRewriter<<Character>>.type_name()
  {1,1,134,101,91,9}, // ListRewriter<<Character>>.init(Character[])
  {136,0,0,0,21,19}, // ListRewriter<<Character>>.has_another()
  {137,0,0,0,32,20}, // ListRewriter<<Character>>.read()
  {9,1,0,29,91,5}, // ListRewriter<<Character>>.write(Character)
  {0,0,0,0,92,1}, // Boxed<<Int32?>>.init_object()
  {1,0,0,0,0,1}, // Boxed<<Int32?>>.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // Boxed<<Int32?>>.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // Boxed<<Int32?>>.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // Boxed<<Int32?>>.type_name()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {1,1,135,62,92,152}, // Boxed<<Int32?>>.init(Int32?)
  {161,0,0,0,117,153}, // Boxed<<Int32?>>.to_Int32?()
  {0,0,0,0,93,1}, // Function_1182.init_object()
  {1,0,0,0,0,1}, // Function_1182.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // Function_1182.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // Function_1182.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // Function_1182.type_name()
  {92,0,0,0,-1,11}, // Function_1182.call()
  {1,1,136,102,93,9}, // Function_1182.init(Console)
  {0,0,0,0,94,1}, // Boxed<<SystemEnvironment>>.init_object()
  {1,0,0,0,0,1}, // Boxed<<SystemEnvironment>>.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // Boxed<<SystemEnvironment>>.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // Boxed<<SystemEnvironment>>.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // Boxed<<SystemEnvironment>>.type_name()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {1,1,137,103,94,163}, // Boxed<<SystemEnvironment>>.init(SystemEnvironment)
  {162,0,0,0,118,164}, // Boxed<<SystemEnvironment>>.to_SystemEnvironment()
  {0,0,0,0,95,1}, // IOError.init_object()
  {1,0,0,0,25,1}, // IOError.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // IOError.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // IOError.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // IOError.type_name()
  {1,1,78,1,25,9}, // IOError.init(String)
  {112,0,0,0,-1,11}, // IOError.display()
  {113,0,0,0,11,1}, // IOError.format()
  {141,0,0,0,95,1}, // IOError._throw()
  {0,0,0,0,96,1}, // FileReader.init_object()
  {1,0,0,0,0,1}, // FileReader.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // FileReader.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // FileReader.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // FileReader.type_name()
  {5,0,0,0,96,1}, // FileReader.close()
  {136,0,0,0,21,19}, // FileReader.has_another()
  {146,0,0,0,16,102}, // FileReader.peek()
  {137,0,0,0,16,102}, // FileReader.read()
  {1,1,138,1,96,9}, // FileReader.init(String)
  {144,0,0,0,-1,11}, // FileReader.on_cleanup()
  {145,1,139,1,21,3}, // FileReader.open(String)
  {0,0,0,0,97,1}, // UTF8Reader.init_object()
  {1,0,0,0,0,1}, // UTF8Reader.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // UTF8Reader.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // UTF8Reader.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // UTF8Reader.type_name()
  {5,0,0,0,97,1}, // UTF8Reader.close()
  {136,0,0,0,21,19}, // UTF8Reader.has_another()
  {146,0,0,0,32,20}, // UTF8Reader.peek()
  {137,0,0,0,32,20}, // UTF8Reader.read()
  {1,1,140,90,97,9}, // UTF8Reader.init(Reader<<Byte>>)
  {0,0,0,0,98,1}, // JSONParseError.init_object()
  {1,0,0,0,25,1}, // JSONParseError.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // JSONParseError.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // JSONParseError.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // JSONParseError.type_name()
  {1,1,141,1,98,9}, // JSONParseError.init(String)
  {112,0,0,0,-1,11}, // JSONParseError.display()
  {113,0,0,0,11,1}, // JSONParseError.format()
  {141,0,0,0,98,1}, // JSONParseError._throw()
  {0,0,0,0,99,1}, // JSONParserBuffer.init_object()
  {1,0,0,0,13,1}, // JSONParserBuffer.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // JSONParserBuffer.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // JSONParserBuffer.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // JSONParserBuffer.type_name()
  {1,1,63,9,13,6}, // JSONParserBuffer.init(Int32)
  {-1,-1,-1,-1,-1,1},
  {93,0,0,0,13,1}, // JSONParserBuffer.clear()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {31,1,10,9,32,77}, // JSONParserBuffer.get(Int32)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {81,1,35,29,117,78}, // JSONParserBuffer.locate(Character)
  {-1,-1,-1,-1,-1,1},
  {94,0,0,0,21,19}, // JSONParserBuffer.needs_indent()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {7,2,64,49,13,79}, // JSONParserBuffer.print(Character,Logical)
  {7,1,0,9,13,6}, // JSONParserBuffer.print(Int32)
  {7,1,0,38,13,7}, // JSONParserBuffer.print(Logical)
  {7,1,0,51,13,8}, // JSONParserBuffer.print(Int64)
  {7,1,0,0,13,9}, // JSONParserBuffer.print(Object)
  {7,1,0,39,13,10}, // JSONParserBuffer.print(Real64)
  {7,2,66,52,13,67}, // JSONParserBuffer.print(Real64,Int32)
  {7,1,0,1,13,9}, // JSONParserBuffer.print(String)
  {95,0,0,0,-1,11}, // JSONParserBuffer.print_indent()
  {96,2,66,52,13,67}, // JSONParserBuffer.print_to_work_bytes(Real64,Int32)
  {97,0,0,0,-1,11}, // JSONParserBuffer.print_work_bytes()
  {8,0,0,0,13,1}, // JSONParserBuffer.println()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {8,1,0,1,13,9}, // JSONParserBuffer.println(String)
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {98,1,68,9,13,6}, // JSONParserBuffer.reserve(Int32)
  {99,0,0,0,-1,11}, // JSONParserBuffer.round_off_work_bytes()
  {100,0,0,0,29,80}, // JSONParserBuffer.scan_work_bytes()
  {0,0,0,0,100,1}, // Boxed<<Byte?>>.init_object()
  {1,0,0,0,0,1}, // Boxed<<Byte?>>.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // Boxed<<Byte?>>.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // Boxed<<Byte?>>.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // Boxed<<Byte?>>.type_name()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {1,1,142,104,100,168}, // Boxed<<Byte?>>.init(Byte?)
  {163,0,0,0,119,169}, // Boxed<<Byte?>>.to_Byte?()
  {0,0,0,0,101,1}, // Boxed<<Value?>>.init_object()
  {1,0,0,0,0,1}, // Boxed<<Value?>>.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // Boxed<<Value?>>.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // Boxed<<Value?>>.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // Boxed<<Value?>>.type_name()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {1,1,143,105,101,170}, // Boxed<<Value?>>.init(Value?)
  {164,0,0,0,120,171}, // Boxed<<Value?>>.to_Value?()
  {0,0,0,0,102,1}, // Boxed<<StringBuilder?>>.init_object()
  {1,0,0,0,0,1}, // Boxed<<StringBuilder?>>.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // Boxed<<StringBuilder?>>.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // Boxed<<StringBuilder?>>.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // Boxed<<StringBuilder?>>.type_name()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {1,1,144,106,102,175}, // Boxed<<StringBuilder?>>.init(StringBuilder?)
  {165,0,0,0,121,176}, // Boxed<<StringBuilder?>>.to_StringBuilder?()
  {0,0,0,0,103,1}, // Boxed<<(Function())?>>.init_object()
  {1,0,0,0,0,1}, // Boxed<<(Function())?>>.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // Boxed<<(Function())?>>.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // Boxed<<(Function())?>>.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // Boxed<<(Function())?>>.type_name()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {1,1,145,107,103,177}, // Boxed<<(Function())?>>.init((Function())?)
  {166,0,0,0,122,178}, // Boxed<<(Function())?>>.to_(Function())?()
  {0,0,0,0,104,1}, // Boxed<<String?>>.init_object()
  {1,0,0,0,0,1}, // Boxed<<String?>>.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // Boxed<<String?>>.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // Boxed<<String?>>.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // Boxed<<String?>>.type_name()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {1,1,146,108,104,179}, // Boxed<<String?>>.init(String?)
  {167,0,0,0,123,180}, // Boxed<<String?>>.to_String?()
  {0,0,0,0,105,1}, // Boxed<<PropertyInfo?>>.init_object()
  {1,0,0,0,0,1}, // Boxed<<PropertyInfo?>>.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // Boxed<<PropertyInfo?>>.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // Boxed<<PropertyInfo?>>.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // Boxed<<PropertyInfo?>>.type_name()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {1,1,147,109,105,184}, // Boxed<<PropertyInfo?>>.init(PropertyInfo?)
  {168,0,0,0,124,185}, // Boxed<<PropertyInfo?>>.to_PropertyInfo?()
  {0,0,0,0,106,1}, // Boxed<<MethodInfo?>>.init_object()
  {1,0,0,0,0,1}, // Boxed<<MethodInfo?>>.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // Boxed<<MethodInfo?>>.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // Boxed<<MethodInfo?>>.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // Boxed<<MethodInfo?>>.type_name()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {1,1,148,110,106,186}, // Boxed<<MethodInfo?>>.init(MethodInfo?)
  {169,0,0,0,125,187}, // Boxed<<MethodInfo?>>.to_MethodInfo?()
  {0,0,0,0,107,1}, // Boxed<<Character?>>.init_object()
  {1,0,0,0,0,1}, // Boxed<<Character?>>.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // Boxed<<Character?>>.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // Boxed<<Character?>>.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // Boxed<<Character?>>.type_name()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {1,1,149,111,107,191}, // Boxed<<Character?>>.init(Character?)
  {170,0,0,0,126,192}, // Boxed<<Character?>>.to_Character?()
  {0,0,0,0,108,1}, // Boxed<<Real64?>>.init_object()
  {1,0,0,0,0,1}, // Boxed<<Real64?>>.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // Boxed<<Real64?>>.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // Boxed<<Real64?>>.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // Boxed<<Real64?>>.type_name()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {1,1,150,112,108,193}, // Boxed<<Real64?>>.init(Real64?)
  {171,0,0,0,127,194}, // Boxed<<Real64?>>.to_Real64?()
  {0,0,0,0,109,1}, // Boxed<<Real32?>>.init_object()
  {1,0,0,0,0,1}, // Boxed<<Real32?>>.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // Boxed<<Real32?>>.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // Boxed<<Real32?>>.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // Boxed<<Real32?>>.type_name()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {1,1,151,113,109,195}, // Boxed<<Real32?>>.init(Real32?)
  {172,0,0,0,128,196}, // Boxed<<Real32?>>.to_Real32?()
  {0,0,0,0,110,1}, // Boxed<<Int64?>>.init_object()
  {1,0,0,0,0,1}, // Boxed<<Int64?>>.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // Boxed<<Int64?>>.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // Boxed<<Int64?>>.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // Boxed<<Int64?>>.type_name()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {1,1,152,114,110,197}, // Boxed<<Int64?>>.init(Int64?)
  {173,0,0,0,129,198}, // Boxed<<Int64?>>.to_Int64?()
  {0,0,0,0,111,1}, // Boxed<<FileOptions>>.init_object()
  {1,0,0,0,0,1}, // Boxed<<FileOptions>>.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // Boxed<<FileOptions>>.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // Boxed<<FileOptions>>.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // Boxed<<FileOptions>>.type_name()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {1,1,153,115,111,120}, // Boxed<<FileOptions>>.init(FileOptions)
  {174,0,0,0,130,202}, // Boxed<<FileOptions>>.to_FileOptions()
  {0,0,0,0,112,1}, // Boxed<<TableEntry<<String,Value>>?>>.init_object()
  {1,0,0,0,0,1}, // Boxed<<TableEntry<<String,Value>>?>>.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // Boxed<<TableEntry<<String,Value>>?>>.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // Boxed<<TableEntry<<String,Value>>?>>.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // Boxed<<TableEntry<<String,Value>>?>>.type_name()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {1,1,154,116,112,203}, // Boxed<<TableEntry<<String,Value>>?>>.init(TableEntry<<String,Value>>?)
  {175,0,0,0,131,204}, // Boxed<<TableEntry<<String,Value>>?>>.to_TableEntry<<String,Value>>?()
  {0,0,0,0,113,1}, // Boxed<<TypeInfo?>>.init_object()
  {1,0,0,0,0,1}, // Boxed<<TypeInfo?>>.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // Boxed<<TypeInfo?>>.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // Boxed<<TypeInfo?>>.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // Boxed<<TypeInfo?>>.type_name()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {1,1,155,117,113,205}, // Boxed<<TypeInfo?>>.init(TypeInfo?)
  {176,0,0,0,132,206}, // Boxed<<TypeInfo?>>.to_TypeInfo?()
  {0,0,0,0,114,1}, // Boxed<<TableEntry<<Int32,String>>?>>.init_object()
  {1,0,0,0,0,1}, // Boxed<<TableEntry<<Int32,String>>?>>.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // Boxed<<TableEntry<<Int32,String>>?>>.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // Boxed<<TableEntry<<Int32,String>>?>>.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // Boxed<<TableEntry<<Int32,String>>?>>.type_name()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {1,1,156,118,114,210}, // Boxed<<TableEntry<<Int32,String>>?>>.init(TableEntry<<Int32,String>>?)
  {177,0,0,0,133,211}, // Boxed<<TableEntry<<Int32,String>>?>>.to_TableEntry<<Int32,String>>?()
  {0,0,0,0,115,1}, // Boxed<<TableEntry<<String,String>>?>>.init_object()
  {1,0,0,0,0,1}, // Boxed<<TableEntry<<String,String>>?>>.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // Boxed<<TableEntry<<String,String>>?>>.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // Boxed<<TableEntry<<String,String>>?>>.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // Boxed<<TableEntry<<String,String>>?>>.type_name()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {1,1,157,119,115,212}, // Boxed<<TableEntry<<String,String>>?>>.init(TableEntry<<String,String>>?)
  {178,0,0,0,134,213}, // Boxed<<TableEntry<<String,String>>?>>.to_TableEntry<<String,String>>?()
  {0,0,0,0,116,1}, // Boxed<<TableEntry<<String,TypeInfo>>?>>.init_object()
  {1,0,0,0,0,1}, // Boxed<<TableEntry<<String,TypeInfo>>?>>.init()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {2,0,0,0,31,13}, // Boxed<<TableEntry<<String,TypeInfo>>?>>.object_id()
  {-1,-1,-1,-1,-1,1},
  {3,0,0,0,11,1}, // Boxed<<TableEntry<<String,TypeInfo>>?>>.to_String()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {4,0,0,0,11,1}, // Boxed<<TableEntry<<String,TypeInfo>>?>>.type_name()
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {-1,-1,-1,-1,-1,1},
  {1,1,158,120,116,214}, // Boxed<<TableEntry<<String,TypeInfo>>?>>.init(TableEntry<<String,TypeInfo>>?)
  {179,0,0,0,135,215}, // Boxed<<TableEntry<<String,TypeInfo>>?>>.to_TableEntry<<String,TypeInfo>>?()
  {180,0,0,0,5,114}, // Int32?.to_Value()
  {3,0,0,0,11,113}, // Int32?.to_String()
  {180,0,0,0,5,118}, // SystemEnvironment.to_Value()
  {31,1,159,1,11,119}, // SystemEnvironment.get(String)
  {3,0,0,0,11,117}, // SystemEnvironment.to_String()
  {180,0,0,0,5,128}, // Byte?.to_Value()
  {3,0,0,0,11,127}, // Byte?.to_String()
  {180,0,0,0,5,132}, // Value?.to_Value()
  {3,0,0,0,11,131}, // Value?.to_String()
  {180,0,0,0,5,135}, // StringBuilder?.to_Value()
  {3,0,0,0,11,134}, // StringBuilder?.to_String()
  {180,0,0,0,5,138}, // (Function())?.to_Value()
  {3,0,0,0,11,137}, // (Function())?.to_String()
  {180,0,0,0,5,141}, // String?.to_Value()
  {3,0,0,0,11,140}, // String?.to_String()
  {180,0,0,0,5,144}, // PropertyInfo?.to_Value()
  {3,0,0,0,11,143}, // PropertyInfo?.to_String()
  {180,0,0,0,5,147}, // MethodInfo?.to_Value()
  {3,0,0,0,11,146}, // MethodInfo?.to_String()
  {180,0,0,0,5,151}, // Character?.to_Value()
  {3,0,0,0,11,150}, // Character?.to_String()
  {180,0,0,0,5,156}, // Real64?.to_Value()
  {3,0,0,0,11,155}, // Real64?.to_String()
  {180,0,0,0,5,159}, // Real32?.to_Value()
  {3,0,0,0,11,158}, // Real32?.to_String()
  {180,0,0,0,5,162}, // Int64?.to_Value()
  {3,0,0,0,11,161}, // Int64?.to_String()
  {180,0,0,0,5,167}, // FileOptions.to_Value()
  {3,0,0,0,11,166}, // FileOptions.to_String()
  {180,0,0,0,5,174}, // TableEntry<<String,Value>>?.to_Value()
  {3,0,0,0,11,173}, // TableEntry<<String,Value>>?.to_String()
  {180,0,0,0,5,183}, // TypeInfo?.to_Value()
  {3,0,0,0,11,182}, // TypeInfo?.to_String()
  {180,0,0,0,5,190}, // TableEntry<<Int32,String>>?.to_Value()
  {3,0,0,0,11,189}, // TableEntry<<Int32,String>>?.to_String()
  {180,0,0,0,5,201}, // TableEntry<<String,String>>?.to_Value()
  {3,0,0,0,11,200}, // TableEntry<<String,String>>?.to_String()
  {180,0,0,0,5,209}, // TableEntry<<String,TypeInfo>>?.to_Value()
  {3,0,0,0,11,208}, // TableEntry<<String,TypeInfo>>?.to_String()
};

const void* Rogue_global_property_pointers[] =
{
  (void*) &RogueStringBuilder_work_bytes,
  (void*) &RogueStringBuilder_pool,
  (void*) &RogueMethodInfo__method_name_strings,
  (void*) &RogueSystem_command_line_arguments,
  (void*) &RogueSystem_executable_filepath,
  (void*) &RogueSystem_environment,
  (void*) &RogueLogicalValue_true_value,
  (void*) &RogueLogicalValue_false_value,
  (void*) &RogueStringValue_empty_string
};

const int Rogue_property_offsets[] =
{
  (int)(intptr_t)&((RogueClassGlobal*)0)->config,
  (int)(intptr_t)&((RogueClassGlobal*)0)->cache,
  (int)(intptr_t)&((RogueClassGlobal*)0)->console,
  (int)(intptr_t)&((RogueClassGlobal*)0)->global_output_buffer,
  (int)(intptr_t)&((RogueClassGlobal*)0)->exit_functions,
  (int)(intptr_t)&((RogueClassValueTable*)0)->data,
  (int)(intptr_t)&((RogueClassTable_String_Value_*)0)->count,
  (int)(intptr_t)&((RogueClassTable_String_Value_*)0)->bin_mask,
  (int)(intptr_t)&((RogueClassTable_String_Value_*)0)->cur_entry_index,
  (int)(intptr_t)&((RogueClassTable_String_Value_*)0)->bins,
  (int)(intptr_t)&((RogueClassTable_String_Value_*)0)->first_entry,
  (int)(intptr_t)&((RogueClassTable_String_Value_*)0)->last_entry,
  (int)(intptr_t)&((RogueClassTable_String_Value_*)0)->cur_entry,
  (int)(intptr_t)&((RogueClassTable_String_Value_*)0)->sort_function,
  (int)(intptr_t)&((RogueClassTableEntry_String_Value_*)0)->key,
  (int)(intptr_t)&((RogueClassTableEntry_String_Value_*)0)->value,
  (int)(intptr_t)&((RogueClassTableEntry_String_Value_*)0)->adjacent_entry,
  (int)(intptr_t)&((RogueClassTableEntry_String_Value_*)0)->next_entry,
  (int)(intptr_t)&((RogueClassTableEntry_String_Value_*)0)->previous_entry,
  (int)(intptr_t)&((RogueClassTableEntry_String_Value_*)0)->hash,
  (int)(intptr_t)&((RogueStringBuilder*)0)->utf8,
  (int)(intptr_t)&((RogueStringBuilder*)0)->count,
  (int)(intptr_t)&((RogueStringBuilder*)0)->indent,
  (int)(intptr_t)&((RogueStringBuilder*)0)->cursor_offset,
  (int)(intptr_t)&((RogueStringBuilder*)0)->cursor_index,
  (int)(intptr_t)&((RogueStringBuilder*)0)->at_newline,
  (int)(intptr_t)&((RogueByte_List*)0)->data,
  (int)(intptr_t)&((RogueByte_List*)0)->count,
  (int)(intptr_t)&((RogueClassStringBuilderPool*)0)->available,
  (int)(intptr_t)&((RogueStringBuilder_List*)0)->data,
  (int)(intptr_t)&((RogueStringBuilder_List*)0)->count,
  (int)(intptr_t)&((Rogue_Function____List*)0)->data,
  (int)(intptr_t)&((Rogue_Function____List*)0)->count,
  (int)(intptr_t)&((RogueException*)0)->message,
  (int)(intptr_t)&((RogueException*)0)->stack_trace,
  (int)(intptr_t)&((RogueClassStackTrace*)0)->entries,
  (int)(intptr_t)&((RogueClassStackTrace*)0)->count,
  (int)(intptr_t)&((RogueClassStackTrace*)0)->is_formatted,
  (int)(intptr_t)&((RogueString_List*)0)->data,
  (int)(intptr_t)&((RogueString_List*)0)->count,
  (int)(intptr_t)&((RogueTypeInfo*)0)->index,
  (int)(intptr_t)&((RogueTypeInfo*)0)->name,
  (int)(intptr_t)&((RogueTypeInfo*)0)->global_properties,
  (int)(intptr_t)&((RogueTypeInfo*)0)->properties,
  (int)(intptr_t)&((RogueTypeInfo*)0)->methods,
  (int)(intptr_t)&((RoguePropertyInfo_List*)0)->data,
  (int)(intptr_t)&((RoguePropertyInfo_List*)0)->count,
  (int)(intptr_t)&((RogueClassPropertyInfo*)0)->index,
  (int)(intptr_t)&((RogueClassPropertyInfo*)0)->property_name_index,
  (int)(intptr_t)&((RogueClassPropertyInfo*)0)->property_type_index,
  (int)(intptr_t)&((RogueMethodInfo_List*)0)->data,
  (int)(intptr_t)&((RogueMethodInfo_List*)0)->count,
  (int)(intptr_t)&((RogueClassMethodInfo*)0)->index,
  (int)(intptr_t)&((RogueClassMethodInfo*)0)->name,
  (int)(intptr_t)&((RogueClassMethodInfo*)0)->base_name,
  (int)(intptr_t)&((RogueClassMethodInfo*)0)->signature,
  (int)(intptr_t)&((RogueClassMethodInfo*)0)->fn_ptr,
  (int)(intptr_t)&((RogueClassMethodInfo*)0)->parameter_count,
  (int)(intptr_t)&((RogueClassMethodInfo*)0)->call_handler,
  (int)(intptr_t)&((RogueClassMethodInfo*)0)->_first_param_name,
  (int)(intptr_t)&((RogueClassMethodInfo*)0)->__first_param_type,
  (int)(intptr_t)&((RogueClassMethodInfo*)0)->__return_type,
  (int)(intptr_t)&((RogueClassTable_Int32_String_*)0)->count,
  (int)(intptr_t)&((RogueClassTable_Int32_String_*)0)->bin_mask,
  (int)(intptr_t)&((RogueClassTable_Int32_String_*)0)->cur_entry_index,
  (int)(intptr_t)&((RogueClassTable_Int32_String_*)0)->bins,
  (int)(intptr_t)&((RogueClassTable_Int32_String_*)0)->first_entry,
  (int)(intptr_t)&((RogueClassTable_Int32_String_*)0)->last_entry,
  (int)(intptr_t)&((RogueClassTable_Int32_String_*)0)->cur_entry,
  (int)(intptr_t)&((RogueClassTable_Int32_String_*)0)->sort_function,
  (int)(intptr_t)&((RogueClassTableEntry_Int32_String_*)0)->key,
  (int)(intptr_t)&((RogueClassTableEntry_Int32_String_*)0)->value,
  (int)(intptr_t)&((RogueClassTableEntry_Int32_String_*)0)->adjacent_entry,
  (int)(intptr_t)&((RogueClassTableEntry_Int32_String_*)0)->next_entry,
  (int)(intptr_t)&((RogueClassTableEntry_Int32_String_*)0)->previous_entry,
  (int)(intptr_t)&((RogueClassTableEntry_Int32_String_*)0)->hash,
  (int)(intptr_t)&((RogueCharacter_List*)0)->data,
  (int)(intptr_t)&((RogueCharacter_List*)0)->count,
  (int)(intptr_t)&((RogueClassValueList*)0)->data,
  (int)(intptr_t)&((RogueValue_List*)0)->data,
  (int)(intptr_t)&((RogueValue_List*)0)->count,
  (int)(intptr_t)&((RogueClassObjectValue*)0)->value,
  (int)(intptr_t)&((RogueClassStringConsolidationTable*)0)->count,
  (int)(intptr_t)&((RogueClassStringConsolidationTable*)0)->bin_mask,
  (int)(intptr_t)&((RogueClassStringConsolidationTable*)0)->cur_entry_index,
  (int)(intptr_t)&((RogueClassStringConsolidationTable*)0)->bins,
  (int)(intptr_t)&((RogueClassStringConsolidationTable*)0)->first_entry,
  (int)(intptr_t)&((RogueClassStringConsolidationTable*)0)->last_entry,
  (int)(intptr_t)&((RogueClassStringConsolidationTable*)0)->cur_entry,
  (int)(intptr_t)&((RogueClassStringConsolidationTable*)0)->sort_function,
  (int)(intptr_t)&((RogueClassStringTable_String_*)0)->count,
  (int)(intptr_t)&((RogueClassStringTable_String_*)0)->bin_mask,
  (int)(intptr_t)&((RogueClassStringTable_String_*)0)->cur_entry_index,
  (int)(intptr_t)&((RogueClassStringTable_String_*)0)->bins,
  (int)(intptr_t)&((RogueClassStringTable_String_*)0)->first_entry,
  (int)(intptr_t)&((RogueClassStringTable_String_*)0)->last_entry,
  (int)(intptr_t)&((RogueClassStringTable_String_*)0)->cur_entry,
  (int)(intptr_t)&((RogueClassStringTable_String_*)0)->sort_function,
  (int)(intptr_t)&((RogueClassTable_String_String_*)0)->count,
  (int)(intptr_t)&((RogueClassTable_String_String_*)0)->bin_mask,
  (int)(intptr_t)&((RogueClassTable_String_String_*)0)->cur_entry_index,
  (int)(intptr_t)&((RogueClassTable_String_String_*)0)->bins,
  (int)(intptr_t)&((RogueClassTable_String_String_*)0)->first_entry,
  (int)(intptr_t)&((RogueClassTable_String_String_*)0)->last_entry,
  (int)(intptr_t)&((RogueClassTable_String_String_*)0)->cur_entry,
  (int)(intptr_t)&((RogueClassTable_String_String_*)0)->sort_function,
  (int)(intptr_t)&((RogueClassTableEntry_String_String_*)0)->key,
  (int)(intptr_t)&((RogueClassTableEntry_String_String_*)0)->value,
  (int)(intptr_t)&((RogueClassTableEntry_String_String_*)0)->adjacent_entry,
  (int)(intptr_t)&((RogueClassTableEntry_String_String_*)0)->next_entry,
  (int)(intptr_t)&((RogueClassTableEntry_String_String_*)0)->previous_entry,
  (int)(intptr_t)&((RogueClassTableEntry_String_String_*)0)->hash,
  (int)(intptr_t)&((RogueClassConsole*)0)->position,
  (int)(intptr_t)&((RogueClassConsole*)0)->error,
  (int)(intptr_t)&((RogueClassConsole*)0)->immediate_mode,
  (int)(intptr_t)&((RogueClassConsole*)0)->is_blocking,
  (int)(intptr_t)&((RogueClassConsole*)0)->decode_bytes,
  (int)(intptr_t)&((RogueClassConsole*)0)->output_buffer,
  (int)(intptr_t)&((RogueClassConsole*)0)->input_buffer,
  (int)(intptr_t)&((RogueClassConsole*)0)->next_input_character,
  (int)(intptr_t)&((RogueClassConsole*)0)->input_bytes,
  0,
  0,
  (int)(intptr_t)&((RogueClassConsoleErrorPrinter*)0)->output_buffer,
  (int)(intptr_t)&((RogueClassPrimitiveWorkBuffer*)0)->utf8,
  (int)(intptr_t)&((RogueClassPrimitiveWorkBuffer*)0)->count,
  (int)(intptr_t)&((RogueClassPrimitiveWorkBuffer*)0)->indent,
  (int)(intptr_t)&((RogueClassPrimitiveWorkBuffer*)0)->cursor_offset,
  (int)(intptr_t)&((RogueClassPrimitiveWorkBuffer*)0)->cursor_index,
  (int)(intptr_t)&((RogueClassPrimitiveWorkBuffer*)0)->at_newline,
  (int)(intptr_t)&((RogueClassError*)0)->message,
  (int)(intptr_t)&((RogueClassError*)0)->stack_trace,
  (int)(intptr_t)&((RogueClassFile*)0)->filepath,
  (int)(intptr_t)&((RogueClassLineReader*)0)->position,
  (int)(intptr_t)&((RogueClassLineReader*)0)->source,
  (int)(intptr_t)&((RogueClassLineReader*)0)->next,
  (int)(intptr_t)&((RogueClassLineReader*)0)->buffer,
  (int)(intptr_t)&((RogueClassLineReader*)0)->prev,
  (int)(intptr_t)&((RogueClassFileWriter*)0)->position,
  (int)(intptr_t)&((RogueClassFileWriter*)0)->filepath,
  (int)(intptr_t)&((RogueClassFileWriter*)0)->error,
  (int)(intptr_t)&((RogueClassFileWriter*)0)->buffer,
  0,
  (int)(intptr_t)&((RogueClassScanner*)0)->position,
  (int)(intptr_t)&((RogueClassScanner*)0)->data,
  (int)(intptr_t)&((RogueClassScanner*)0)->count,
  (int)(intptr_t)&((RogueClassScanner*)0)->line,
  (int)(intptr_t)&((RogueClassScanner*)0)->column,
  (int)(intptr_t)&((RogueClassScanner*)0)->spaces_per_tab,
  (int)(intptr_t)&((RogueClassJSONParser*)0)->reader,
  (int)(intptr_t)&((RogueWeakReference*)0)->next_weak_reference,
  0,
  (int)(intptr_t)&((RogueClassPrintWriterAdapter*)0)->output,
  (int)(intptr_t)&((RogueClassPrintWriterAdapter*)0)->buffer,
  (int)(intptr_t)&((RogueClassLogicalValue*)0)->value,
  (int)(intptr_t)&((RogueClassInt32Value*)0)->value,
  (int)(intptr_t)&((RogueClassInt64Value*)0)->value,
  (int)(intptr_t)&((RogueClassReal64Value*)0)->value,
  (int)(intptr_t)&((RogueClassStringValue*)0)->value,
  (int)(intptr_t)&((RogueClassOutOfBoundsError*)0)->message,
  (int)(intptr_t)&((RogueClassOutOfBoundsError*)0)->stack_trace,
  (int)(intptr_t)&((RogueClassTableEntry_String_TypeInfo_*)0)->key,
  (int)(intptr_t)&((RogueClassTableEntry_String_TypeInfo_*)0)->value,
  (int)(intptr_t)&((RogueClassTableEntry_String_TypeInfo_*)0)->adjacent_entry,
  (int)(intptr_t)&((RogueClassTableEntry_String_TypeInfo_*)0)->next_entry,
  (int)(intptr_t)&((RogueClassTableEntry_String_TypeInfo_*)0)->previous_entry,
  (int)(intptr_t)&((RogueClassTableEntry_String_TypeInfo_*)0)->hash,
  (int)(intptr_t)&((RogueClassRequirementError*)0)->message,
  (int)(intptr_t)&((RogueClassRequirementError*)0)->stack_trace,
  (int)(intptr_t)&((RogueClassListRewriter_Character_*)0)->list,
  (int)(intptr_t)&((RogueClassListRewriter_Character_*)0)->read_index,
  (int)(intptr_t)&((RogueClassListRewriter_Character_*)0)->write_index,
  (int)(intptr_t)&((RogueClassOptionalBoxed_Int32_*)0)->value,
  (int)(intptr_t)&((RogueClassFunction_1182*)0)->console,
  (int)(intptr_t)&((RogueClassBoxed_SystemEnvironment_*)0)->value,
  (int)(intptr_t)&((RogueClassIOError*)0)->message,
  (int)(intptr_t)&((RogueClassIOError*)0)->stack_trace,
  (int)(intptr_t)&((RogueClassFileReader*)0)->position,
  (int)(intptr_t)&((RogueClassFileReader*)0)->filepath,
  (int)(intptr_t)&((RogueClassFileReader*)0)->count,
  (int)(intptr_t)&((RogueClassFileReader*)0)->buffer_position,
  (int)(intptr_t)&((RogueClassFileReader*)0)->buffer,
  0,
  (int)(intptr_t)&((RogueClassUTF8Reader*)0)->position,
  (int)(intptr_t)&((RogueClassUTF8Reader*)0)->byte_reader,
  (int)(intptr_t)&((RogueClassUTF8Reader*)0)->next,
  (int)(intptr_t)&((RogueClassJSONParseError*)0)->message,
  (int)(intptr_t)&((RogueClassJSONParseError*)0)->stack_trace,
  (int)(intptr_t)&((RogueClassJSONParserBuffer*)0)->utf8,
  (int)(intptr_t)&((RogueClassJSONParserBuffer*)0)->count,
  (int)(intptr_t)&((RogueClassJSONParserBuffer*)0)->indent,
  (int)(intptr_t)&((RogueClassJSONParserBuffer*)0)->cursor_offset,
  (int)(intptr_t)&((RogueClassJSONParserBuffer*)0)->cursor_index,
  (int)(intptr_t)&((RogueClassJSONParserBuffer*)0)->at_newline,
  (int)(intptr_t)&((RogueClassOptionalBoxed_Byte_*)0)->value,
  (int)(intptr_t)&((RogueClassOptionalBoxed_Value_*)0)->value,
  (int)(intptr_t)&((RogueClassOptionalBoxed_StringBuilder_*)0)->value,
  (int)(intptr_t)&((RogueClassOptionalBoxed__Function____*)0)->value,
  (int)(intptr_t)&((RogueClassOptionalBoxed_String_*)0)->value,
  (int)(intptr_t)&((RogueClassOptionalBoxed_PropertyInfo_*)0)->value,
  (int)(intptr_t)&((RogueClassOptionalBoxed_MethodInfo_*)0)->value,
  (int)(intptr_t)&((RogueClassOptionalBoxed_Character_*)0)->value,
  (int)(intptr_t)&((RogueClassOptionalBoxed_Real64_*)0)->value,
  (int)(intptr_t)&((RogueClassOptionalBoxed_Real32_*)0)->value,
  (int)(intptr_t)&((RogueClassOptionalBoxed_Int64_*)0)->value,
  (int)(intptr_t)&((RogueClassBoxed_FileOptions_*)0)->value,
  (int)(intptr_t)&((RogueClassOptionalBoxed_TableEntry_String_Value__*)0)->value,
  (int)(intptr_t)&((RogueClassOptionalBoxed_TypeInfo_*)0)->value,
  (int)(intptr_t)&((RogueClassOptionalBoxed_TableEntry_Int32_String__*)0)->value,
  (int)(intptr_t)&((RogueClassOptionalBoxed_TableEntry_String_String__*)0)->value,
  (int)(intptr_t)&((RogueClassOptionalBoxed_TableEntry_String_TypeInfo__*)0)->value,
  (int)(intptr_t)&((RogueOptionalInt32*)0)->value,
  (int)(intptr_t)&((RogueOptionalInt32*)0)->exists,
  (int)(intptr_t)&((RogueOptionalByte*)0)->value,
  (int)(intptr_t)&((RogueOptionalByte*)0)->exists,
  (int)(intptr_t)&((RogueOptionalValue*)0)->value,
  (int)(intptr_t)&((RogueOptionalValue*)0)->exists,
  (int)(intptr_t)&((RogueOptionalStringBuilder*)0)->value,
  (int)(intptr_t)&((RogueOptionalStringBuilder*)0)->exists,
  (int)(intptr_t)&((RogueOptional_Function___*)0)->value,
  (int)(intptr_t)&((RogueOptional_Function___*)0)->exists,
  (int)(intptr_t)&((RogueOptionalString*)0)->value,
  (int)(intptr_t)&((RogueOptionalString*)0)->exists,
  (int)(intptr_t)&((RogueOptionalPropertyInfo*)0)->value,
  (int)(intptr_t)&((RogueOptionalPropertyInfo*)0)->exists,
  (int)(intptr_t)&((RogueOptionalMethodInfo*)0)->value,
  (int)(intptr_t)&((RogueOptionalMethodInfo*)0)->exists,
  (int)(intptr_t)&((RogueOptionalCharacter*)0)->value,
  (int)(intptr_t)&((RogueOptionalCharacter*)0)->exists,
  (int)(intptr_t)&((RogueOptionalReal64*)0)->value,
  (int)(intptr_t)&((RogueOptionalReal64*)0)->exists,
  (int)(intptr_t)&((RogueOptionalReal32*)0)->value,
  (int)(intptr_t)&((RogueOptionalReal32*)0)->exists,
  (int)(intptr_t)&((RogueOptionalInt64*)0)->value,
  (int)(intptr_t)&((RogueOptionalInt64*)0)->exists,
  (int)(intptr_t)&((RogueClassFileOptions*)0)->flags,
  (int)(intptr_t)&((RogueOptionalTableEntry_String_Value_*)0)->value,
  (int)(intptr_t)&((RogueOptionalTableEntry_String_Value_*)0)->exists,
  (int)(intptr_t)&((RogueOptionalTypeInfo*)0)->value,
  (int)(intptr_t)&((RogueOptionalTypeInfo*)0)->exists,
  (int)(intptr_t)&((RogueOptionalTableEntry_Int32_String_*)0)->value,
  (int)(intptr_t)&((RogueOptionalTableEntry_Int32_String_*)0)->exists,
  (int)(intptr_t)&((RogueOptionalTableEntry_String_String_*)0)->value,
  (int)(intptr_t)&((RogueOptionalTableEntry_String_String_*)0)->exists,
  (int)(intptr_t)&((RogueOptionalTableEntry_String_TypeInfo_*)0)->value,
  (int)(intptr_t)&((RogueOptionalTableEntry_String_TypeInfo_*)0)->exists
};

const int Rogue_object_size_table[136] =
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
  (int) sizeof(RogueReal32),
  (int) sizeof(RogueInt64),
  (int) sizeof(RogueCharacter),
  (int) sizeof(RogueTypeInfo),
  (int) sizeof(RoguePropertyInfo_List),
  (int) sizeof(RogueClassPropertyInfo),
  (int) sizeof(RogueArray),
  (int) sizeof(RogueMethodInfo_List),
  (int) sizeof(RogueClassMethodInfo),
  (int) sizeof(RogueClassTable_Int32_String_),
  (int) sizeof(RogueArray),
  (int) sizeof(RogueClassTableEntry_Int32_String_),
  (int) sizeof(RogueClass_Function_TableEntry_Int32_String__TableEntry_Int32_String__RETURNSLogical_),
  (int) sizeof(RogueArray),
  (int) sizeof(RogueCharacter_List),
  (int) sizeof(RogueArray),
  (int) sizeof(RogueClassValueList),
  (int) sizeof(RogueValue_List),
  (int) sizeof(RogueArray),
  (int) sizeof(RogueClassObjectValue),
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
  (int) sizeof(RogueClassBoxed),
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
  (int) sizeof(RogueClassFunction_281),
  (int) sizeof(RogueClassRuntime),
  (int) sizeof(RogueWeakReference),
  (int) sizeof(RogueClassPrintWriterAdapter),
  (int) sizeof(RogueClassPrintWriter_buffer_),
  (int) sizeof(RogueClassLogicalValue),
  (int) sizeof(RogueClassInt32Value),
  (int) sizeof(RogueClassInt64Value),
  (int) sizeof(RogueClassReal64Value),
  (int) sizeof(RogueClassNullValue),
  (int) sizeof(RogueClassStringValue),
  (int) sizeof(RogueClassUndefinedValue),
  (int) sizeof(RogueClassOutOfBoundsError),
  (int) sizeof(RogueClassTableEntry_String_TypeInfo_),
  (int) sizeof(RogueClassRequirementError),
  (int) sizeof(RogueClassListRewriter_Character_),
  (int) sizeof(RogueClassOptionalBoxed_Int32_),
  (int) sizeof(RogueClassFunction_1182),
  (int) sizeof(RogueClassBoxed_SystemEnvironment_),
  (int) sizeof(RogueClassIOError),
  (int) sizeof(RogueClassFileReader),
  (int) sizeof(RogueClassUTF8Reader),
  (int) sizeof(RogueClassJSONParseError),
  (int) sizeof(RogueClassJSONParserBuffer),
  (int) sizeof(RogueClassOptionalBoxed_Byte_),
  (int) sizeof(RogueClassOptionalBoxed_Value_),
  (int) sizeof(RogueClassOptionalBoxed_StringBuilder_),
  (int) sizeof(RogueClassOptionalBoxed__Function____),
  (int) sizeof(RogueClassOptionalBoxed_String_),
  (int) sizeof(RogueClassOptionalBoxed_PropertyInfo_),
  (int) sizeof(RogueClassOptionalBoxed_MethodInfo_),
  (int) sizeof(RogueClassOptionalBoxed_Character_),
  (int) sizeof(RogueClassOptionalBoxed_Real64_),
  (int) sizeof(RogueClassOptionalBoxed_Real32_),
  (int) sizeof(RogueClassOptionalBoxed_Int64_),
  (int) sizeof(RogueClassBoxed_FileOptions_),
  (int) sizeof(RogueClassOptionalBoxed_TableEntry_String_Value__),
  (int) sizeof(RogueClassOptionalBoxed_TypeInfo_),
  (int) sizeof(RogueClassOptionalBoxed_TableEntry_Int32_String__),
  (int) sizeof(RogueClassOptionalBoxed_TableEntry_String_String__),
  (int) sizeof(RogueClassOptionalBoxed_TableEntry_String_TypeInfo__),
  (int) sizeof(RogueOptionalInt32),
  (int) sizeof(RogueClassSystemEnvironment),
  (int) sizeof(RogueOptionalByte),
  (int) sizeof(RogueOptionalValue),
  (int) sizeof(RogueOptionalStringBuilder),
  (int) sizeof(RogueOptional_Function___),
  (int) sizeof(RogueOptionalString),
  (int) sizeof(RogueOptionalPropertyInfo),
  (int) sizeof(RogueOptionalMethodInfo),
  (int) sizeof(RogueOptionalCharacter),
  (int) sizeof(RogueOptionalReal64),
  (int) sizeof(RogueOptionalReal32),
  (int) sizeof(RogueOptionalInt64),
  (int) sizeof(RogueClassFileOptions),
  (int) sizeof(RogueOptionalTableEntry_String_Value_),
  (int) sizeof(RogueOptionalTypeInfo),
  (int) sizeof(RogueOptionalTableEntry_Int32_String_),
  (int) sizeof(RogueOptionalTableEntry_String_String_),
  (int) sizeof(RogueOptionalTableEntry_String_TypeInfo_)
};

const int Rogue_attributes_table[136] =
{
  32,
  9216,
  1,
  1,
  0,
  0,
  0,
  8194,
  32,
  32,
  0,
  32,
  0,
  8192,
  0,
  0,
  8194,
  32,
  1024,
  0,
  32,
  8194,
  0,
  0,
  32,
  8192,
  8192,
  8192,
  32,
  8194,
  8194,
  8194,
  8194,
  0,
  0,
  0,
  32,
  0,
  8192,
  0,
  32,
  0,
  0,
  32,
  8192,
  32,
  0,
  0,
  32,
  0,
  1024,
  0,
  0,
  32,
  0,
  0,
  1,
  1,
  1024,
  1,
  0,
  1024,
  0,
  131072,
  1024,
  8192,
  0,
  0,
  0,
  1,
  0,
  1,
  0,
  0,
  0,
  0,
  1024,
  8192,
  8192,
  0,
  1,
  0,
  0,
  0,
  0,
  1024,
  0,
  1024,
  0,
  0,
  0,
  0,
  8192,
  0,
  8192,
  0,
  0,
  0,
  0,
  1024,
  8192,
  8192,
  8192,
  8192,
  8192,
  8192,
  8192,
  8192,
  8192,
  8192,
  8192,
  8192,
  8192,
  8192,
  8192,
  8192,
  8192,
  8195,
  8195,
  8195,
  8195,
  8195,
  8195,
  8195,
  8195,
  8195,
  8195,
  8195,
  8195,
  8195,
  8195,
  8195,
  8195,
  8195,
  8195,
  8195
};

int Rogue_allocator_count = 1;
RogueAllocator Rogue_allocators[1];

int Rogue_type_count = 136;
RogueType Rogue_types[136];

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
RogueType* RogueTypeReal32;
RogueType* RogueTypeInt64;
RogueType* RogueTypeCharacter;
RogueType* RogueTypeTypeInfo;
RogueType* RogueTypePropertyInfo_List;
RogueType* RogueTypePropertyInfo;
RogueType* RogueTypeMethodInfo_List;
RogueType* RogueTypeMethodInfo;
RogueType* RogueTypeTable_Int32_String_;
RogueType* RogueTypeTableEntry_Int32_String_;
RogueType* RogueType_Function_TableEntry_Int32_String__TableEntry_Int32_String__RETURNSLogical_;
RogueType* RogueTypeCharacter_List;
RogueType* RogueTypeValueList;
RogueType* RogueTypeValue_List;
RogueType* RogueTypeObjectValue;
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
RogueType* RogueTypeBoxed;
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
RogueType* RogueTypeFunction_281;
RogueType* RogueTypeRuntime;
RogueType* RogueTypeWeakReference;
RogueType* RogueTypePrintWriterAdapter;
RogueType* RogueTypePrintWriter_buffer_;
RogueType* RogueTypeLogicalValue;
RogueType* RogueTypeInt32Value;
RogueType* RogueTypeInt64Value;
RogueType* RogueTypeReal64Value;
RogueType* RogueTypeNullValue;
RogueType* RogueTypeStringValue;
RogueType* RogueTypeUndefinedValue;
RogueType* RogueTypeOutOfBoundsError;
RogueType* RogueTypeTableEntry_String_TypeInfo_;
RogueType* RogueTypeRequirementError;
RogueType* RogueTypeListRewriter_Character_;
RogueType* RogueTypeOptionalBoxed_Int32_;
RogueType* RogueTypeFunction_1182;
RogueType* RogueTypeBoxed_SystemEnvironment_;
RogueType* RogueTypeIOError;
RogueType* RogueTypeFileReader;
RogueType* RogueTypeUTF8Reader;
RogueType* RogueTypeJSONParseError;
RogueType* RogueTypeJSONParserBuffer;
RogueType* RogueTypeOptionalBoxed_Byte_;
RogueType* RogueTypeOptionalBoxed_Value_;
RogueType* RogueTypeOptionalBoxed_StringBuilder_;
RogueType* RogueTypeOptionalBoxed__Function____;
RogueType* RogueTypeOptionalBoxed_String_;
RogueType* RogueTypeOptionalBoxed_PropertyInfo_;
RogueType* RogueTypeOptionalBoxed_MethodInfo_;
RogueType* RogueTypeOptionalBoxed_Character_;
RogueType* RogueTypeOptionalBoxed_Real64_;
RogueType* RogueTypeOptionalBoxed_Real32_;
RogueType* RogueTypeOptionalBoxed_Int64_;
RogueType* RogueTypeBoxed_FileOptions_;
RogueType* RogueTypeOptionalBoxed_TableEntry_String_Value__;
RogueType* RogueTypeOptionalBoxed_TypeInfo_;
RogueType* RogueTypeOptionalBoxed_TableEntry_Int32_String__;
RogueType* RogueTypeOptionalBoxed_TableEntry_String_String__;
RogueType* RogueTypeOptionalBoxed_TableEntry_String_TypeInfo__;
RogueType* RogueTypeOptionalInt32;
RogueType* RogueTypeSystemEnvironment;
RogueType* RogueTypeOptionalByte;
RogueType* RogueTypeOptionalValue;
RogueType* RogueTypeOptionalStringBuilder;
RogueType* RogueTypeOptional_Function___;
RogueType* RogueTypeOptionalString;
RogueType* RogueTypeOptionalPropertyInfo;
RogueType* RogueTypeOptionalMethodInfo;
RogueType* RogueTypeOptionalCharacter;
RogueType* RogueTypeOptionalReal64;
RogueType* RogueTypeOptionalReal32;
RogueType* RogueTypeOptionalInt64;
RogueType* RogueTypeFileOptions;
RogueType* RogueTypeOptionalTableEntry_String_Value_;
RogueType* RogueTypeOptionalTypeInfo;
RogueType* RogueTypeOptionalTableEntry_Int32_String_;
RogueType* RogueTypeOptionalTableEntry_String_String_;
RogueType* RogueTypeOptionalTableEntry_String_TypeInfo_;

int Rogue_literal_string_count = 480;
RogueString* Rogue_literal_strings[480];

RogueClassPrintWriter* RoguePrintWriter__create__Writer_Byte_( RogueClassWriter_Byte_* writer_0 )
{
  ROGUE_GC_CHECK;
  return (RogueClassPrintWriter*)(((RogueClassPrintWriter*)(((RogueClassPrintWriterAdapter*)(RoguePrintWriterAdapter__init__Writer_Byte_( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassPrintWriterAdapter*,ROGUE_CREATE_OBJECT(PrintWriterAdapter))), ((writer_0)) ))))));
}

RogueClassValue* RogueValue__create__Character( RogueCharacter value_0 )
{
  ROGUE_GC_CHECK;
  return (RogueClassValue*)((RogueValue__create__String( ROGUE_ARG(((RogueString*)(RogueCharacter__to_String( value_0 )))) )));
}

RogueClassValue* RogueValue__create__Logical( RogueLogical value_0 )
{
  ROGUE_GC_CHECK;
  return (RogueClassValue*)(((RogueClassValue*)(((((value_0))) ? (ROGUE_ARG(RogueLogicalValue_true_value)) : ROGUE_ARG(RogueLogicalValue_false_value)))));
}

RogueClassValue* RogueValue__create__Byte( RogueByte value_0 )
{
  ROGUE_GC_CHECK;
  return (RogueClassValue*)(((RogueClassValue*)(((RogueClassInt32Value*)(RogueInt32Value__init__Int32( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassInt32Value*,ROGUE_CREATE_OBJECT(Int32Value))), ROGUE_ARG(((RogueInt32)(value_0))) ))))));
}

RogueClassValue* RogueValue__create__Int32( RogueInt32 value_0 )
{
  ROGUE_GC_CHECK;
  return (RogueClassValue*)(((RogueClassValue*)(((RogueClassInt32Value*)(RogueInt32Value__init__Int32( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassInt32Value*,ROGUE_CREATE_OBJECT(Int32Value))), value_0 ))))));
}

RogueClassValue* RogueValue__create__Int64( RogueInt64 value_0 )
{
  ROGUE_GC_CHECK;
  return (RogueClassValue*)(((RogueClassValue*)(((RogueClassInt64Value*)(RogueInt64Value__init__Int64( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassInt64Value*,ROGUE_CREATE_OBJECT(Int64Value))), value_0 ))))));
}

RogueClassValue* RogueValue__create__Real32( RogueReal32 value_0 )
{
  ROGUE_GC_CHECK;
  return (RogueClassValue*)(((RogueClassValue*)(((RogueClassReal64Value*)(RogueReal64Value__init__Real64( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassReal64Value*,ROGUE_CREATE_OBJECT(Real64Value))), ROGUE_ARG(((RogueReal64)(value_0))) ))))));
}

RogueClassValue* RogueValue__create__Real64( RogueReal64 value_0 )
{
  ROGUE_GC_CHECK;
  return (RogueClassValue*)(((RogueClassValue*)(((RogueClassReal64Value*)(RogueReal64Value__init__Real64( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassReal64Value*,ROGUE_CREATE_OBJECT(Real64Value))), value_0 ))))));
}

RogueClassValue* RogueValue__create__Object( RogueObject* value_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((void*)value_0) == ((void*)NULL)))
  {
    return (RogueClassValue*)(((RogueClassValue*)(((RogueClassNullValue*)ROGUE_SINGLETON(NullValue)))));
  }
  return (RogueClassValue*)(((RogueClassValue*)(((RogueClassObjectValue*)(RogueObjectValue__init__Object( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassObjectValue*,ROGUE_CREATE_OBJECT(ObjectValue))), value_0 ))))));
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
  return (RogueLogical)(((((((void*)value_0) != ((void*)NULL)) && ((Rogue_call_ROGUEM9( 45, value_0 ))))) && (((!((Rogue_call_ROGUEM9( 43, value_0 )))) || ((Rogue_call_ROGUEM9( 108, value_0 )))))));
}

RogueInt32 RogueInt32__create__Real64( RogueReal64 value_0 )
{
  ROGUE_GC_CHECK;
  return (RogueInt32)(((RogueInt32)(value_0)));
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

RogueByte RogueByte__create__Real64( RogueReal64 value_0 )
{
  ROGUE_GC_CHECK;
  return (RogueByte)(((RogueByte)(value_0)));
}

RogueLogical RogueLogical__create__Real64( RogueReal64 value_0 )
{
  ROGUE_GC_CHECK;
  return (RogueLogical)(((value_0) != (0.0)));
}

RogueLogical RogueLogical__create__Int32( RogueInt32 value_0 )
{
  ROGUE_GC_CHECK;
  return (RogueLogical)(((value_0) != (0)));
}

RogueReal64 RogueReal64__create__Real64( RogueReal64 value_0 )
{
  ROGUE_GC_CHECK;
  return (RogueReal64)(value_0);
}

RogueReal32 RogueReal32__create__Real64( RogueReal64 value_0 )
{
  ROGUE_GC_CHECK;
  return (RogueReal32)(((RogueReal32)(value_0)));
}

RogueInt64 RogueInt64__create__Real64( RogueReal64 value_0 )
{
  ROGUE_GC_CHECK;
  return (RogueInt64)(((RogueInt64)(value_0)));
}

RogueCharacter RogueCharacter__create__Real64( RogueReal64 value_0 )
{
  ROGUE_GC_CHECK;
  return (RogueCharacter)(((RogueCharacter)(value_0)));
}

RogueCharacter RogueCharacter__create__Int32( RogueInt32 value_0 )
{
  ROGUE_GC_CHECK;
  return (RogueCharacter)(((RogueCharacter)(value_0)));
}

RogueTypeInfo* RogueTypeInfo__get__Int32( RogueInt32 index_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((((index_0) < (0))) || (((index_0) >= ((RogueTypeInfo__type_count())))))))
  {
    return (RogueTypeInfo*)(((RogueTypeInfo*)(NULL)));
  }
  return (RogueTypeInfo*)(((RogueTypeInfo*)(RogueType_type_info(&Rogue_types[index_0]))));
}

RogueInt32 RogueTypeInfo__type_count()
{
  ROGUE_GC_CHECK;
  return (RogueInt32)(((RogueInt32)(Rogue_type_count)));
}

RogueString* RogueMethodInfo___get_method_name__Int32( RogueInt32 method_index_0 )
{
  ROGUE_GC_CHECK;
  ROGUE_DEF_LOCAL_REF(RogueString*,name_1,(((RogueString*)(RogueTable_Int32_String___get__Int32( ROGUE_ARG(RogueMethodInfo__method_name_strings), method_index_0 )))));
  if (ROGUE_COND(!(!!(name_1))))
  {
    int name_index = Rogue_method_info_table[method_index_0][0];
    if (name_index >= 0) name_1 = RogueString_create_from_utf8(Rogue_method_name_strings[name_index]);

    ROGUE_DEF_LOCAL_REF_NULL(RogueString*,_auto_94_2);
    ((_auto_94_2=(name_1))?_auto_94_2:throw ((RogueClassRequirementError*)(RogueRequirementError___throw( ROGUE_ARG(((RogueClassRequirementError*)(RogueRequirementError__init__String( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassRequirementError*,ROGUE_CREATE_OBJECT(RequirementError))), Rogue_literal_strings[130] )))) ))));
    RogueTable_Int32_String___set__Int32_String( ROGUE_ARG(RogueMethodInfo__method_name_strings), method_index_0, name_1 );
  }
  return (RogueString*)(name_1);
}

RogueClassValue* RogueMethodInfo__call__Int64_Int32_Value( RogueInt64 fn_0, RogueInt32 i_1, RogueClassValue* args_2 )
{
  ROGUE_GC_CHECK;
  {
    switch (i_1)
    {
      case 0:
      {
        return (RogueClassValue*)((RogueValue__create__String( ROGUE_ARG(((RogueString*)((ROGUEM1)(intptr_t)fn_0)(ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 )))))) )));
      }
      case 1:
      {
        return (RogueClassValue*)((RogueValue__create__String( ROGUE_ARG(((RogueString*)((RogueObject*)((ROGUEM1)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))))))) )));
      }
      case 2:
      {
        return (RogueClassValue*)((RogueValue__create__String( ROGUE_ARG(((RogueString*)((ROGUEM34)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))),ROGUE_ARG((Rogue_call_ROGUEM7( 107, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) ))),ROGUE_ARG((Rogue_call_ROGUEM7( 107, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 2 ))) )))))) )));
      }
      case 3:
      {
        return (RogueClassValue*)((RogueValue__create__Logical( ROGUE_ARG((((ROGUEM6)(intptr_t)fn_0)(ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 )))) ))),ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 )))) )))))) )));
      }
      case 4:
      {
        return (RogueClassValue*)((RogueValue__create__String( ROGUE_ARG(((RogueString*)((RogueByte_List*)((ROGUEM49)(intptr_t)fn_0)(ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 )))) ))),ROGUE_ARG(((RogueValue__to_Byte( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) )))))))) )));
      }
      case 5:
      {
        return (RogueClassValue*)((RogueValue__create__String( ROGUE_ARG(((RogueString*)((ROGUEM31)(intptr_t)fn_0)(ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 )))) ))),ROGUE_ARG((Rogue_call_ROGUEM12( 105, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) )))))) )));
      }
      case 6:
      {
        return (RogueClassValue*)((RogueValue__create__String( ROGUE_ARG(((RogueString*)((ROGUEM8)(intptr_t)fn_0)(ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 )))) ))),ROGUE_ARG((Rogue_call_ROGUEM7( 107, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) )))))) )));
      }
      case 7:
      {
        return (RogueClassValue*)((RogueValue__create__String( ROGUE_ARG(((RogueString*)((ROGUEM38)(intptr_t)fn_0)(ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 )))) ))),ROGUE_ARG((Rogue_call_ROGUEM9( 108, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) )))))) )));
      }
      case 8:
      {
        return (RogueClassValue*)((RogueValue__create__String( ROGUE_ARG(((RogueString*)((RogueStringBuilder*)((ROGUEM46)(intptr_t)fn_0)(ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 )))) ))),ROGUE_ARG((Rogue_call_ROGUEM2( 106, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) ))))))) )));
      }
      case 9:
      {
        return (RogueClassValue*)((RogueValue__create__String( ROGUE_ARG(((RogueString*)((ROGUEM3)(intptr_t)fn_0)(ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 )))) ))),ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) )))))) )));
      }
      case 10:
      {
        return (RogueClassValue*)((RogueValue__create__String( ROGUE_ARG(((RogueString*)((ROGUEM39)(intptr_t)fn_0)(ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 )))) ))),ROGUE_ARG((Rogue_call_ROGUEM13( 109, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) )))))) )));
      }
      case 11:
      {
        (((ROGUEM0)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) )))));
        break;
      }
      case 12:
      {
        return (RogueClassValue*)((RogueValue__create__Int32( ROGUE_ARG((((ROGUEM7)(intptr_t)fn_0)(ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 )))) )))))) )));
      }
      case 13:
      {
        return (RogueClassValue*)((RogueValue__create__Int64( ROGUE_ARG((((ROGUEM2)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) )))))) )));
      }
      case 14:
      {
        return (RogueClassValue*)(((RogueClassValue*)((ROGUEM1)(intptr_t)fn_0)(ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 )))) ))))));
      }
      case 15:
      {
        (((ROGUEM4)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))),ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 )))));
        break;
      }
      case 16:
      {
        return (RogueClassValue*)((RogueValue__create__Logical( ROGUE_ARG((((ROGUEM32)(intptr_t)fn_0)(ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 )))) ))),ROGUE_ARG((Rogue_call_ROGUEM12( 105, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) )))))) )));
      }
      case 17:
      {
        return (RogueClassValue*)((RogueValue__create__Logical( ROGUE_ARG((((ROGUEM33)(intptr_t)fn_0)(ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 )))) ))),ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 )))) ))),ROGUE_ARG((Rogue_call_ROGUEM7( 107, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 2 ))) )))))) )));
      }
      case 18:
      {
        return (RogueClassValue*)((RogueValue__create__Logical( ROGUE_ARG((((ROGUEM79)(intptr_t)fn_0)(ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 )))) ))),ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 )))) ))),ROGUE_ARG((Rogue_call_ROGUEM9( 108, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 2 ))) )))))) )));
      }
      case 19:
      {
        return (RogueClassValue*)((RogueValue__create__Logical( ROGUE_ARG((((ROGUEM9)(intptr_t)fn_0)(ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 )))) )))))) )));
      }
      case 20:
      {
        return (RogueClassValue*)((RogueValue__create__Character( ROGUE_ARG((((ROGUEM12)(intptr_t)fn_0)(ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 )))) )))))) )));
      }
      case 21:
      {
        return (RogueClassValue*)((RogueValue__create__String( ROGUE_ARG(((RogueString*)((ROGUEM35)(intptr_t)fn_0)(ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 )))) ))),ROGUE_ARG((Rogue_call_ROGUEM7( 107, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) ))),ROGUE_ARG((Rogue_call_ROGUEM12( 105, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 2 ))) )))))) )));
      }
      case 22:
      {
        return (RogueClassValue*)(((RogueClassValue*)(RogueOptionalInt32__to_Value( (((ROGUEM36)(intptr_t)fn_0)(ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 )))) ))),ROGUE_ARG((Rogue_call_ROGUEM12( 105, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) ))),(RogueOptionalInt32__from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 2 ))) )))) ))));
      }
      case 23:
      {
        return (RogueClassValue*)(((RogueClassValue*)(RogueOptionalInt32__to_Value( (((ROGUEM37)(intptr_t)fn_0)(ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 )))) ))),ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 )))) ))),(RogueOptionalInt32__from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 2 ))) )))) ))));
      }
      case 24:
      {
        return (RogueClassValue*)((RogueValue__create__String( ROGUE_ARG(((RogueString*)((ROGUEM5)(intptr_t)fn_0)(ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 )))) ))),ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 )))) ))),ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 2 )))) )))))) )));
      }
      case 25:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueString_List*)((ROGUEM40)(intptr_t)fn_0)(ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 )))) ))),ROGUE_ARG((Rogue_call_ROGUEM7( 107, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) ))),ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 2 )))) )))))))) )));
      }
      case 26:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueStringBuilder*)((ROGUEM41)(intptr_t)fn_0)(ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 )))) ))),ROGUE_ARG((Rogue_call_ROGUEM7( 107, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) ))),ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 2 ))) ))),ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 3 )))) )))))))) )));
      }
      case 27:
      {
        return (RogueClassValue*)((RogueValue__create__Real64( ROGUE_ARG((((ROGUEM58)(intptr_t)fn_0)(ROGUE_ARG((Rogue_call_ROGUEM13( 109, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) )))))) )));
      }
      case 28:
      {
        return (RogueClassValue*)((RogueValue__create__Real64( ROGUE_ARG((((ROGUEM13)(intptr_t)fn_0)(ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 )))))) )));
      }
      case 29:
      {
        return (RogueClassValue*)((RogueValue__create__Real64( ROGUE_ARG((((ROGUEM74)(intptr_t)fn_0)(ROGUE_ARG((Rogue_call_ROGUEM13( 109, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))),ROGUE_ARG((Rogue_call_ROGUEM13( 109, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) )))))) )));
      }
      case 30:
      {
        return (RogueClassValue*)((RogueValue__create__Int32( ROGUE_ARG((((ROGUEM28)(intptr_t)fn_0)(ROGUE_ARG((Rogue_call_ROGUEM13( 109, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) )))))) )));
      }
      case 31:
      {
        return (RogueClassValue*)((RogueValue__create__String( ROGUE_ARG(((RogueString*)((ROGUEM57)(intptr_t)fn_0)(ROGUE_ARG((Rogue_call_ROGUEM13( 109, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))),(RogueOptionalInt32__from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) ))))) )));
      }
      case 32:
      {
        return (RogueClassValue*)((RogueValue__create__Int64( ROGUE_ARG((((ROGUEM62)(intptr_t)fn_0)(ROGUE_ARG((Rogue_call_ROGUEM13( 109, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) )))))) )));
      }
      case 33:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueString*)((ROGUEM24)(intptr_t)fn_0)(ROGUE_ARG((Rogue_call_ROGUEM13( 109, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) )))))))) )));
      }
      case 34:
      {
        return (RogueClassValue*)((RogueValue__create__Logical( ROGUE_ARG((((ROGUEM54)(intptr_t)fn_0)(ROGUE_ARG((Rogue_call_ROGUEM13( 109, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) )))))) )));
      }
      case 35:
      {
        return (RogueClassValue*)(((RogueClassValue*)((ROGUEM24)(intptr_t)fn_0)(ROGUE_ARG((Rogue_call_ROGUEM13( 109, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))))));
      }
      case 36:
      {
        return (RogueClassValue*)((RogueValue__create__Real32( ROGUE_ARG((((ROGUEM59)(intptr_t)fn_0)(ROGUE_ARG((Rogue_call_ROGUEM13( 109, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) )))))) )));
      }
      case 37:
      {
        return (RogueClassValue*)((RogueValue__create__Real32( ROGUE_ARG((((ROGUEM14)(intptr_t)fn_0)(ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 )))))) )));
      }
      case 38:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueString*)((ROGUEM23)(intptr_t)fn_0)(ROGUE_ARG(((RogueValue__to_Real32( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))))))))) )));
      }
      case 39:
      {
        return (RogueClassValue*)(((RogueClassValue*)((ROGUEM23)(intptr_t)fn_0)(ROGUE_ARG(((RogueValue__to_Real32( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) )))))));
      }
      case 40:
      {
        return (RogueClassValue*)((RogueValue__create__Int64( ROGUE_ARG((((ROGUEM2)(intptr_t)fn_0)(ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 )))))) )));
      }
      case 41:
      {
        return (RogueClassValue*)((RogueValue__create__Int64( ROGUE_ARG((((ROGUEM73)(intptr_t)fn_0)(ROGUE_ARG((Rogue_call_ROGUEM2( 106, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))),ROGUE_ARG((Rogue_call_ROGUEM2( 106, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) )))))) )));
      }
      case 42:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueString*)((ROGUEM22)(intptr_t)fn_0)(ROGUE_ARG((Rogue_call_ROGUEM2( 106, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) )))))))) )));
      }
      case 43:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueStringBuilder*)((ROGUEM60)(intptr_t)fn_0)(ROGUE_ARG((Rogue_call_ROGUEM2( 106, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))),ROGUE_ARG((Rogue_call_ROGUEM7( 107, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) ))),ROGUE_ARG((Rogue_call_ROGUEM7( 107, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 2 ))) ))),ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 3 ))) )))))))) )));
      }
      case 44:
      {
        return (RogueClassValue*)(((RogueClassValue*)((ROGUEM22)(intptr_t)fn_0)(ROGUE_ARG((Rogue_call_ROGUEM2( 106, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))))));
      }
      case 45:
      {
        return (RogueClassValue*)((RogueValue__create__String( ROGUE_ARG(((RogueString*)((ROGUEM61)(intptr_t)fn_0)(ROGUE_ARG((Rogue_call_ROGUEM2( 106, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))),ROGUE_ARG((Rogue_call_ROGUEM7( 107, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) )))))) )));
      }
      case 46:
      {
        return (RogueClassValue*)((RogueValue__create__Int32( ROGUE_ARG((((ROGUEM25)(intptr_t)fn_0)(ROGUE_ARG((Rogue_call_ROGUEM7( 107, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) )))))) )));
      }
      case 47:
      {
        return (RogueClassValue*)((RogueValue__create__Int32( ROGUE_ARG((((ROGUEM7)(intptr_t)fn_0)(ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 )))))) )));
      }
      case 48:
      {
        return (RogueClassValue*)((RogueValue__create__Int32( ROGUE_ARG((((ROGUEM26)(intptr_t)fn_0)(ROGUE_ARG((Rogue_call_ROGUEM7( 107, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))),ROGUE_ARG((Rogue_call_ROGUEM7( 107, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) )))))) )));
      }
      case 49:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueString*)((ROGUEM21)(intptr_t)fn_0)(ROGUE_ARG((Rogue_call_ROGUEM7( 107, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) )))))))) )));
      }
      case 50:
      {
        return (RogueClassValue*)((RogueValue__create__Logical( ROGUE_ARG((((ROGUEM55)(intptr_t)fn_0)(ROGUE_ARG((Rogue_call_ROGUEM7( 107, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) )))))) )));
      }
      case 51:
      {
        return (RogueClassValue*)(((RogueClassValue*)((ROGUEM21)(intptr_t)fn_0)(ROGUE_ARG((Rogue_call_ROGUEM7( 107, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))))));
      }
      case 52:
      {
        return (RogueClassValue*)((RogueValue__create__Character( ROGUE_ARG((((ROGUEM27)(intptr_t)fn_0)(ROGUE_ARG((Rogue_call_ROGUEM7( 107, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) )))))) )));
      }
      case 53:
      {
        return (RogueClassValue*)((RogueValue__create__Character( ROGUE_ARG((((ROGUEM66)(intptr_t)fn_0)(ROGUE_ARG((Rogue_call_ROGUEM13( 109, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) )))))) )));
      }
      case 54:
      {
        return (RogueClassValue*)((RogueValue__create__Character( ROGUE_ARG((((ROGUEM12)(intptr_t)fn_0)(ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 )))))) )));
      }
      case 55:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueString*)((ROGUEM18)(intptr_t)fn_0)(ROGUE_ARG((Rogue_call_ROGUEM12( 105, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) )))))))) )));
      }
      case 56:
      {
        return (RogueClassValue*)((RogueValue__create__Logical( ROGUE_ARG((((ROGUEM64)(intptr_t)fn_0)(ROGUE_ARG((Rogue_call_ROGUEM12( 105, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) )))))) )));
      }
      case 57:
      {
        return (RogueClassValue*)((RogueValue__create__Logical( ROGUE_ARG((((ROGUEM63)(intptr_t)fn_0)(ROGUE_ARG((Rogue_call_ROGUEM12( 105, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))),ROGUE_ARG((Rogue_call_ROGUEM9( 108, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) ))),ROGUE_ARG((Rogue_call_ROGUEM9( 108, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 2 ))) )))))) )));
      }
      case 58:
      {
        return (RogueClassValue*)(((RogueClassValue*)((ROGUEM18)(intptr_t)fn_0)(ROGUE_ARG((Rogue_call_ROGUEM12( 105, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))))));
      }
      case 59:
      {
        return (RogueClassValue*)((RogueValue__create__Int32( ROGUE_ARG((((ROGUEM65)(intptr_t)fn_0)(ROGUE_ARG((Rogue_call_ROGUEM12( 105, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))),ROGUE_ARG((Rogue_call_ROGUEM7( 107, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) )))))) )));
      }
      case 60:
      {
        return (RogueClassValue*)((RogueValue__create__Byte( ROGUE_ARG((((ROGUEM53)(intptr_t)fn_0)(ROGUE_ARG((Rogue_call_ROGUEM13( 109, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) )))))) )));
      }
      case 61:
      {
        return (RogueClassValue*)((RogueValue__create__Byte( ROGUE_ARG((((ROGUEM11)(intptr_t)fn_0)(ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 )))))) )));
      }
      case 62:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueString*)((ROGUEM20)(intptr_t)fn_0)(ROGUE_ARG(((RogueValue__to_Byte( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))))))))) )));
      }
      case 63:
      {
        return (RogueClassValue*)(((RogueClassValue*)((ROGUEM20)(intptr_t)fn_0)(ROGUE_ARG(((RogueValue__to_Byte( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) )))))));
      }
      case 64:
      {
        return (RogueClassValue*)((RogueValue__create__Logical( ROGUE_ARG((((ROGUEM9)(intptr_t)fn_0)(ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 )))))) )));
      }
      case 65:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueString*)((ROGUEM19)(intptr_t)fn_0)(ROGUE_ARG((Rogue_call_ROGUEM9( 108, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) )))))))) )));
      }
      case 66:
      {
        return (RogueClassValue*)(((RogueClassValue*)((ROGUEM19)(intptr_t)fn_0)(ROGUE_ARG((Rogue_call_ROGUEM9( 108, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))))));
      }
      case 67:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueStringBuilder*)((ROGUEM47)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))),ROGUE_ARG((Rogue_call_ROGUEM13( 109, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) ))),ROGUE_ARG((Rogue_call_ROGUEM7( 107, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 2 ))) )))))))) )));
      }
      case 68:
      {
        (((ROGUEM75)(intptr_t)fn_0)(ROGUE_ARG((Rogue_call_ROGUEM7( 107, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) )))));
        break;
      }
      case 69:
      {
        return (RogueClassValue*)((RogueValue__create__String( ROGUE_ARG(((RogueString*)((ROGUEM76)(intptr_t)fn_0)())) )));
      }
      case 70:
      {
        (((ROGUEM48)(intptr_t)fn_0)());
        break;
      }
      case 71:
      {
        return (RogueClassValue*)((RogueValue__create__String( ROGUE_ARG(((RogueString*)((ROGUEM5)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))),ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))),ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 2 )))) )))))) )));
      }
      case 72:
      {
        return (RogueClassValue*)((RogueValue__create__String( ROGUE_ARG(((RogueString*)((ROGUEM3)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))),ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 )))))) )));
      }
      case 73:
      {
        (((ROGUEM4)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))),ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) )))));
        break;
      }
      case 74:
      {
        (((ROGUEM29)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))),ROGUE_ARG((Rogue_call_ROGUEM7( 107, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) ))),ROGUE_ARG((Rogue_call_ROGUEM7( 107, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 2 ))) )))));
        break;
      }
      case 75:
      {
        return (RogueClassValue*)((RogueValue__create__Int32( ROGUE_ARG((((ROGUEM68)(intptr_t)fn_0)())) )));
      }
      case 76:
      {
        (((ROGUEM67)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))),ROGUE_ARG((Rogue_call_ROGUEM7( 107, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) )))));
        break;
      }
      case 77:
      {
        return (RogueClassValue*)((RogueValue__create__Character( ROGUE_ARG((((ROGUEM43)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))),ROGUE_ARG((Rogue_call_ROGUEM7( 107, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) )))))) )));
      }
      case 78:
      {
        return (RogueClassValue*)(((RogueClassValue*)(RogueOptionalInt32__to_Value( (((ROGUEM44)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))),ROGUE_ARG((Rogue_call_ROGUEM12( 105, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) ))))) ))));
      }
      case 79:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueStringBuilder*)((ROGUEM45)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))),ROGUE_ARG((Rogue_call_ROGUEM12( 105, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) ))),ROGUE_ARG((Rogue_call_ROGUEM9( 108, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 2 ))) )))))))) )));
      }
      case 80:
      {
        return (RogueClassValue*)((RogueValue__create__Real64( ROGUE_ARG((((ROGUEM13)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) )))))) )));
      }
      case 81:
      {
        return (RogueClassValue*)(((RogueClassValue*)(((RogueClassLogicalValue*)((ROGUEM1)(intptr_t)fn_0)(ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))))))));
      }
      case 82:
      {
        return (RogueClassValue*)((RogueValue__create__Logical( ROGUE_ARG((((ROGUEM6)(intptr_t)fn_0)(ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))),ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) )))))) )));
      }
      case 83:
      {
        return (RogueClassValue*)(((RogueClassValue*)((ROGUEM3)(intptr_t)fn_0)(ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))),ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))))));
      }
      case 84:
      {
        return (RogueClassValue*)(((RogueClassValue*)(((RogueClassValueTable*)((ROGUEM3)(intptr_t)fn_0)(ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))),ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) ))))))));
      }
      case 85:
      {
        return (RogueClassValue*)((RogueValue__create__Logical( ROGUE_ARG((((ROGUEM6)(intptr_t)fn_0)(ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))),ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 )))))) )));
      }
      case 86:
      {
        return (RogueClassValue*)(((RogueClassValue*)((ROGUEM8)(intptr_t)fn_0)(ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))),ROGUE_ARG((Rogue_call_ROGUEM7( 107, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) ))))));
      }
      case 87:
      {
        return (RogueClassValue*)(((RogueClassValue*)(((RogueClassReal64Value*)((ROGUEM39)(intptr_t)fn_0)(ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))),ROGUE_ARG((Rogue_call_ROGUEM13( 109, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) ))))))));
      }
      case 88:
      {
        return (RogueClassValue*)((RogueValue__create__Logical( ROGUE_ARG((((ROGUEM10)(intptr_t)fn_0)(ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))),ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) ))),ROGUE_ARG((Rogue_call_ROGUEM9( 108, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 2 ))) ))),ROGUE_ARG((Rogue_call_ROGUEM9( 108, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 3 ))) )))))) )));
      }
      case 89:
      {
        return (RogueClassValue*)(((RogueClassValue*)((ROGUEM5)(intptr_t)fn_0)(ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))),ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 )))) ))),ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 2 ))))));
      }
      case 90:
      {
        return (RogueClassValue*)((RogueValue__create__String( ROGUE_ARG(((RogueString*)((ROGUEM15)(intptr_t)fn_0)(ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))),ROGUE_ARG((Rogue_call_ROGUEM9( 108, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) ))),ROGUE_ARG((Rogue_call_ROGUEM9( 108, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 2 ))) )))))) )));
      }
      case 91:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueStringBuilder*)((ROGUEM16)(intptr_t)fn_0)(ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))),ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) ))),ROGUE_ARG((Rogue_call_ROGUEM9( 108, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 2 ))) ))),ROGUE_ARG((Rogue_call_ROGUEM9( 108, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 3 ))) )))))))) )));
      }
      case 92:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueStringBuilder*)((ROGUEM17)(intptr_t)fn_0)(ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))),ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) ))),ROGUE_ARG((Rogue_call_ROGUEM7( 107, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 2 ))) )))))))) )));
      }
      case 93:
      {
        return (RogueClassValue*)(((RogueClassValue*)(((RogueClassLogicalValue*)((ROGUEM38)(intptr_t)fn_0)(ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))),ROGUE_ARG((Rogue_call_ROGUEM9( 108, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) ))))))));
      }
      case 94:
      {
        return (RogueClassValue*)(((RogueClassValue*)((ROGUEM8)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))),ROGUE_ARG((Rogue_call_ROGUEM7( 107, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) ))))));
      }
      case 95:
      {
        return (RogueClassValue*)(((RogueClassValue*)((ROGUEM3)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))),ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 )))) ))))));
      }
      case 96:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueClassTable_String_Value_*)((ROGUEM5)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))),ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 )))) ))),ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 2 )))))))) )));
      }
      case 97:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueClassTableEntry_String_Value_*)((ROGUEM30)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))),ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 )))) ))),ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 2 ))),ROGUE_ARG((Rogue_call_ROGUEM7( 107, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 3 ))) )))))))) )));
      }
      case 98:
      {
        return (RogueClassValue*)((RogueValue__create__Logical( ROGUE_ARG((((ROGUEM42)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))),ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) ))),ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 2 ))) )))))) )));
      }
      case 99:
      {
        return (RogueClassValue*)((RogueValue__create__Byte( ROGUE_ARG((((ROGUEM51)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))),ROGUE_ARG((Rogue_call_ROGUEM7( 107, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) )))))) )));
      }
      case 100:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueByte_List*)((ROGUEM50)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))),ROGUE_ARG(((RogueValue__to_Byte( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) )))),ROGUE_ARG((Rogue_call_ROGUEM7( 107, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 2 ))) )))))))) )));
      }
      case 101:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueStringBuilder*)((ROGUEM17)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))),ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) ))),ROGUE_ARG((Rogue_call_ROGUEM7( 107, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 2 ))) )))))))) )));
      }
      case 102:
      {
        return (RogueClassValue*)((RogueValue__create__Byte( ROGUE_ARG((((ROGUEM11)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) )))))) )));
      }
      case 103:
      {
        return (RogueClassValue*)(((RogueClassValue*)(RogueOptionalInt32__to_Value( (((ROGUEM56)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))),ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) ))))) ))));
      }
      case 104:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueByte_List*)((ROGUEM52)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))),ROGUE_ARG((Rogue_call_ROGUEM7( 107, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) ))),(RogueOptionalInt32__from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 2 ))) )),ROGUE_ARG((Rogue_call_ROGUEM7( 107, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 3 ))) ))),(RogueOptionalByte__from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 4 ))) ))))))) )));
      }
      case 105:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueClassPropertyInfo*)((ROGUEM69)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))),ROGUE_ARG((Rogue_call_ROGUEM7( 107, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) ))),ROGUE_ARG((Rogue_call_ROGUEM7( 107, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 2 ))) ))),ROGUE_ARG((Rogue_call_ROGUEM7( 107, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 3 ))) )))))))) )));
      }
      case 106:
      {
        return (RogueClassValue*)(((RogueClassValue*)((ROGUEM71)(intptr_t)fn_0)(ROGUE_ARG((Rogue_call_ROGUEM2( 106, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))),ROGUE_ARG((Rogue_call_ROGUEM7( 107, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) ))),ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 2 ))))));
      }
      case 107:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueClassMethodInfo*)((ROGUEM70)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))),ROGUE_ARG((Rogue_call_ROGUEM7( 107, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) ))),ROGUE_ARG((Rogue_call_ROGUEM7( 107, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 2 ))) ))),ROGUE_ARG((Rogue_call_ROGUEM7( 107, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 3 ))) ))),ROGUE_ARG((Rogue_call_ROGUEM7( 107, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 4 ))) ))),ROGUE_ARG((Rogue_call_ROGUEM7( 107, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 5 ))) ))),ROGUE_ARG((Rogue_call_ROGUEM7( 107, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 6 ))) )))))))) )));
      }
      case 108:
      {
        return (RogueClassValue*)(((RogueClassValue*)((ROGUEM3)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))),ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))))));
      }
      case 109:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueClassTableEntry_Int32_String_*)((ROGUEM72)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))),ROGUE_ARG((Rogue_call_ROGUEM7( 107, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) ))),ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 2 )))) ))),ROGUE_ARG((Rogue_call_ROGUEM7( 107, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 3 ))) )))))))) )));
      }
      case 110:
      {
        return (RogueClassValue*)(((RogueClassValue*)(RogueOptionalInt32__to_Value( (((ROGUEM56)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))),ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))))) ))));
      }
      case 111:
      {
        return (RogueClassValue*)(((RogueClassValue*)(RogueOptionalInt32__to_Value( (((ROGUEM121)(intptr_t)fn_0)()) ))));
      }
      case 112:
      {
        return (RogueClassValue*)(((RogueClassValue*)(RogueOptionalInt32__to_Value( (((ROGUEM83)(intptr_t)fn_0)(ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))))) ))));
      }
      case 113:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueString*)((ROGUEM120)(intptr_t)fn_0)((RogueOptionalInt32__from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))))))) )));
      }
      case 114:
      {
        return (RogueClassValue*)(((RogueClassValue*)((ROGUEM120)(intptr_t)fn_0)((RogueOptionalInt32__from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) )))));
      }
      case 115:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueClassTableEntry_String_String_*)((ROGUEM30)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))),ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 )))) ))),ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 2 )))) ))),ROGUE_ARG((Rogue_call_ROGUEM7( 107, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 3 ))) )))))))) )));
      }
      case 116:
      {
        return (RogueClassValue*)(((RogueClassValue*)(RogueSystemEnvironment__to_Value( (((ROGUEM85)(intptr_t)fn_0)(ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))))) ))));
      }
      case 117:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueString*)((ROGUEM122)(intptr_t)fn_0)((RogueSystemEnvironment__from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))))))) )));
      }
      case 118:
      {
        return (RogueClassValue*)(((RogueClassValue*)((ROGUEM122)(intptr_t)fn_0)((RogueSystemEnvironment__from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) )))));
      }
      case 119:
      {
        return (RogueClassValue*)((RogueValue__create__String( ROGUE_ARG(((RogueString*)((ROGUEM123)(intptr_t)fn_0)((RogueSystemEnvironment__from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) )),ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 )))) )))))) )));
      }
      case 120:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueClassBoxed_FileOptions_*)((ROGUEM108)(intptr_t)fn_0)(ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 )))) ))),(RogueFileOptions__from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) ))))))) )));
      }
      case 121:
      {
        return (RogueClassValue*)((RogueValue__create__Logical( ROGUE_ARG((((ROGUEM77)(intptr_t)fn_0)(ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 )))) ))),ROGUE_ARG((Rogue_call_ROGUEM7( 107, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) ))),ROGUE_ARG((Rogue_call_ROGUEM7( 107, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 2 ))) ))),ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 3 )))) ))),ROGUE_ARG((Rogue_call_ROGUEM7( 107, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 4 ))) ))),ROGUE_ARG((Rogue_call_ROGUEM7( 107, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 5 ))) )))))) )));
      }
      case 122:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueClassFileWriter*)((ROGUEM78)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))),ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 )))) ))),ROGUE_ARG((Rogue_call_ROGUEM9( 108, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 2 ))) )))))))) )));
      }
      case 123:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueClassScanner*)((ROGUEM80)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))),ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 )))) ))),ROGUE_ARG((Rogue_call_ROGUEM7( 107, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 2 ))) ))),ROGUE_ARG((Rogue_call_ROGUEM9( 108, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 3 ))) )))))))) )));
      }
      case 124:
      {
        return (RogueClassValue*)(((RogueClassValue*)((ROGUEM81)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))),ROGUE_ARG((Rogue_call_ROGUEM12( 105, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) ))),ROGUE_ARG((Rogue_call_ROGUEM12( 105, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 2 ))) ))))));
      }
      case 125:
      {
        return (RogueClassValue*)(((RogueClassValue*)(RogueOptionalByte__to_Value( (((ROGUEM125)(intptr_t)fn_0)()) ))));
      }
      case 126:
      {
        return (RogueClassValue*)(((RogueClassValue*)(RogueOptionalByte__to_Value( (((ROGUEM87)(intptr_t)fn_0)(ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))))) ))));
      }
      case 127:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueString*)((ROGUEM124)(intptr_t)fn_0)((RogueOptionalByte__from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))))))) )));
      }
      case 128:
      {
        return (RogueClassValue*)(((RogueClassValue*)((ROGUEM124)(intptr_t)fn_0)((RogueOptionalByte__from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) )))));
      }
      case 129:
      {
        return (RogueClassValue*)(((RogueClassValue*)(((RogueClassInt64Value*)((ROGUEM46)(intptr_t)fn_0)(ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))),ROGUE_ARG((Rogue_call_ROGUEM2( 106, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) ))))))));
      }
      case 130:
      {
        return (RogueClassValue*)(((RogueClassValue*)(RogueOptionalValue__to_Value( (((ROGUEM89)(intptr_t)fn_0)(ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))))) ))));
      }
      case 131:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueString*)((ROGUEM126)(intptr_t)fn_0)((RogueOptionalValue__from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))))))) )));
      }
      case 132:
      {
        return (RogueClassValue*)(((RogueClassValue*)((ROGUEM126)(intptr_t)fn_0)((RogueOptionalValue__from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) )))));
      }
      case 133:
      {
        return (RogueClassValue*)(((RogueClassValue*)(RogueOptionalStringBuilder__to_Value( (((ROGUEM91)(intptr_t)fn_0)(ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))))) ))));
      }
      case 134:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueString*)((ROGUEM127)(intptr_t)fn_0)((RogueOptionalStringBuilder__from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))))))) )));
      }
      case 135:
      {
        return (RogueClassValue*)(((RogueClassValue*)((ROGUEM127)(intptr_t)fn_0)((RogueOptionalStringBuilder__from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) )))));
      }
      case 136:
      {
        return (RogueClassValue*)(((RogueClassValue*)(RogueOptional_Function_____to_Value( (((ROGUEM93)(intptr_t)fn_0)(ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))))) ))));
      }
      case 137:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueString*)((ROGUEM128)(intptr_t)fn_0)((RogueOptional_Function_____from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))))))) )));
      }
      case 138:
      {
        return (RogueClassValue*)(((RogueClassValue*)((ROGUEM128)(intptr_t)fn_0)((RogueOptional_Function_____from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) )))));
      }
      case 139:
      {
        return (RogueClassValue*)(((RogueClassValue*)(RogueOptionalString__to_Value( (((ROGUEM95)(intptr_t)fn_0)(ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))))) ))));
      }
      case 140:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueString*)((ROGUEM129)(intptr_t)fn_0)((RogueOptionalString__from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))))))) )));
      }
      case 141:
      {
        return (RogueClassValue*)(((RogueClassValue*)((ROGUEM129)(intptr_t)fn_0)((RogueOptionalString__from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) )))));
      }
      case 142:
      {
        return (RogueClassValue*)(((RogueClassValue*)(RogueOptionalPropertyInfo__to_Value( (((ROGUEM97)(intptr_t)fn_0)(ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))))) ))));
      }
      case 143:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueString*)((ROGUEM130)(intptr_t)fn_0)((RogueOptionalPropertyInfo__from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))))))) )));
      }
      case 144:
      {
        return (RogueClassValue*)(((RogueClassValue*)((ROGUEM130)(intptr_t)fn_0)((RogueOptionalPropertyInfo__from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) )))));
      }
      case 145:
      {
        return (RogueClassValue*)(((RogueClassValue*)(RogueOptionalMethodInfo__to_Value( (((ROGUEM99)(intptr_t)fn_0)(ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))))) ))));
      }
      case 146:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueString*)((ROGUEM131)(intptr_t)fn_0)((RogueOptionalMethodInfo__from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))))))) )));
      }
      case 147:
      {
        return (RogueClassValue*)(((RogueClassValue*)((ROGUEM131)(intptr_t)fn_0)((RogueOptionalMethodInfo__from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) )))));
      }
      case 148:
      {
        return (RogueClassValue*)(((RogueClassValue*)(RogueOptionalCharacter__to_Value( (((ROGUEM133)(intptr_t)fn_0)()) ))));
      }
      case 149:
      {
        return (RogueClassValue*)(((RogueClassValue*)(RogueOptionalCharacter__to_Value( (((ROGUEM101)(intptr_t)fn_0)(ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))))) ))));
      }
      case 150:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueString*)((ROGUEM132)(intptr_t)fn_0)((RogueOptionalCharacter__from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))))))) )));
      }
      case 151:
      {
        return (RogueClassValue*)(((RogueClassValue*)((ROGUEM132)(intptr_t)fn_0)((RogueOptionalCharacter__from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) )))));
      }
      case 152:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueClassOptionalBoxed_Int32_*)((ROGUEM82)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))),(RogueOptionalInt32__from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) ))))))) )));
      }
      case 153:
      {
        return (RogueClassValue*)(((RogueClassValue*)(RogueOptionalInt32__to_Value( (((ROGUEM83)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))))) ))));
      }
      case 154:
      {
        return (RogueClassValue*)(((RogueClassValue*)(RogueOptionalReal64__to_Value( (((ROGUEM103)(intptr_t)fn_0)(ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))))) ))));
      }
      case 155:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueString*)((ROGUEM134)(intptr_t)fn_0)((RogueOptionalReal64__from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))))))) )));
      }
      case 156:
      {
        return (RogueClassValue*)(((RogueClassValue*)((ROGUEM134)(intptr_t)fn_0)((RogueOptionalReal64__from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) )))));
      }
      case 157:
      {
        return (RogueClassValue*)(((RogueClassValue*)(RogueOptionalReal32__to_Value( (((ROGUEM105)(intptr_t)fn_0)(ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))))) ))));
      }
      case 158:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueString*)((ROGUEM135)(intptr_t)fn_0)((RogueOptionalReal32__from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))))))) )));
      }
      case 159:
      {
        return (RogueClassValue*)(((RogueClassValue*)((ROGUEM135)(intptr_t)fn_0)((RogueOptionalReal32__from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) )))));
      }
      case 160:
      {
        return (RogueClassValue*)(((RogueClassValue*)(RogueOptionalInt64__to_Value( (((ROGUEM107)(intptr_t)fn_0)(ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))))) ))));
      }
      case 161:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueString*)((ROGUEM136)(intptr_t)fn_0)((RogueOptionalInt64__from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))))))) )));
      }
      case 162:
      {
        return (RogueClassValue*)(((RogueClassValue*)((ROGUEM136)(intptr_t)fn_0)((RogueOptionalInt64__from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) )))));
      }
      case 163:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueClassBoxed_SystemEnvironment_*)((ROGUEM84)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))),(RogueSystemEnvironment__from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) ))))))) )));
      }
      case 164:
      {
        return (RogueClassValue*)(((RogueClassValue*)(RogueSystemEnvironment__to_Value( (((ROGUEM85)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))))) ))));
      }
      case 165:
      {
        return (RogueClassValue*)(((RogueClassValue*)(RogueFileOptions__to_Value( (((ROGUEM109)(intptr_t)fn_0)(ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))))) ))));
      }
      case 166:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueString*)((ROGUEM137)(intptr_t)fn_0)((RogueFileOptions__from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))))))) )));
      }
      case 167:
      {
        return (RogueClassValue*)(((RogueClassValue*)((ROGUEM137)(intptr_t)fn_0)((RogueFileOptions__from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) )))));
      }
      case 168:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueClassOptionalBoxed_Byte_*)((ROGUEM86)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))),(RogueOptionalByte__from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) ))))))) )));
      }
      case 169:
      {
        return (RogueClassValue*)(((RogueClassValue*)(RogueOptionalByte__to_Value( (((ROGUEM87)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))))) ))));
      }
      case 170:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueClassOptionalBoxed_Value_*)((ROGUEM88)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))),(RogueOptionalValue__from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) ))))))) )));
      }
      case 171:
      {
        return (RogueClassValue*)(((RogueClassValue*)(RogueOptionalValue__to_Value( (((ROGUEM89)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))))) ))));
      }
      case 172:
      {
        return (RogueClassValue*)(((RogueClassValue*)(RogueOptionalTableEntry_String_Value___to_Value( (((ROGUEM111)(intptr_t)fn_0)(ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))))) ))));
      }
      case 173:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueString*)((ROGUEM138)(intptr_t)fn_0)((RogueOptionalTableEntry_String_Value___from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))))))) )));
      }
      case 174:
      {
        return (RogueClassValue*)(((RogueClassValue*)((ROGUEM138)(intptr_t)fn_0)((RogueOptionalTableEntry_String_Value___from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) )))));
      }
      case 175:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueClassOptionalBoxed_StringBuilder_*)((ROGUEM90)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))),(RogueOptionalStringBuilder__from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) ))))))) )));
      }
      case 176:
      {
        return (RogueClassValue*)(((RogueClassValue*)(RogueOptionalStringBuilder__to_Value( (((ROGUEM91)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))))) ))));
      }
      case 177:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueClassOptionalBoxed__Function____*)((ROGUEM92)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))),(RogueOptional_Function_____from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) ))))))) )));
      }
      case 178:
      {
        return (RogueClassValue*)(((RogueClassValue*)(RogueOptional_Function_____to_Value( (((ROGUEM93)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))))) ))));
      }
      case 179:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueClassOptionalBoxed_String_*)((ROGUEM94)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))),(RogueOptionalString__from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) ))))))) )));
      }
      case 180:
      {
        return (RogueClassValue*)(((RogueClassValue*)(RogueOptionalString__to_Value( (((ROGUEM95)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))))) ))));
      }
      case 181:
      {
        return (RogueClassValue*)(((RogueClassValue*)(RogueOptionalTypeInfo__to_Value( (((ROGUEM113)(intptr_t)fn_0)(ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))))) ))));
      }
      case 182:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueString*)((ROGUEM139)(intptr_t)fn_0)((RogueOptionalTypeInfo__from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))))))) )));
      }
      case 183:
      {
        return (RogueClassValue*)(((RogueClassValue*)((ROGUEM139)(intptr_t)fn_0)((RogueOptionalTypeInfo__from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) )))));
      }
      case 184:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueClassOptionalBoxed_PropertyInfo_*)((ROGUEM96)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))),(RogueOptionalPropertyInfo__from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) ))))))) )));
      }
      case 185:
      {
        return (RogueClassValue*)(((RogueClassValue*)(RogueOptionalPropertyInfo__to_Value( (((ROGUEM97)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))))) ))));
      }
      case 186:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueClassOptionalBoxed_MethodInfo_*)((ROGUEM98)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))),(RogueOptionalMethodInfo__from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) ))))))) )));
      }
      case 187:
      {
        return (RogueClassValue*)(((RogueClassValue*)(RogueOptionalMethodInfo__to_Value( (((ROGUEM99)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))))) ))));
      }
      case 188:
      {
        return (RogueClassValue*)(((RogueClassValue*)(RogueOptionalTableEntry_Int32_String___to_Value( (((ROGUEM115)(intptr_t)fn_0)(ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))))) ))));
      }
      case 189:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueString*)((ROGUEM140)(intptr_t)fn_0)((RogueOptionalTableEntry_Int32_String___from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))))))) )));
      }
      case 190:
      {
        return (RogueClassValue*)(((RogueClassValue*)((ROGUEM140)(intptr_t)fn_0)((RogueOptionalTableEntry_Int32_String___from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) )))));
      }
      case 191:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueClassOptionalBoxed_Character_*)((ROGUEM100)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))),(RogueOptionalCharacter__from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) ))))))) )));
      }
      case 192:
      {
        return (RogueClassValue*)(((RogueClassValue*)(RogueOptionalCharacter__to_Value( (((ROGUEM101)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))))) ))));
      }
      case 193:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueClassOptionalBoxed_Real64_*)((ROGUEM102)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))),(RogueOptionalReal64__from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) ))))))) )));
      }
      case 194:
      {
        return (RogueClassValue*)(((RogueClassValue*)(RogueOptionalReal64__to_Value( (((ROGUEM103)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))))) ))));
      }
      case 195:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueClassOptionalBoxed_Real32_*)((ROGUEM104)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))),(RogueOptionalReal32__from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) ))))))) )));
      }
      case 196:
      {
        return (RogueClassValue*)(((RogueClassValue*)(RogueOptionalReal32__to_Value( (((ROGUEM105)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))))) ))));
      }
      case 197:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueClassOptionalBoxed_Int64_*)((ROGUEM106)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))),(RogueOptionalInt64__from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) ))))))) )));
      }
      case 198:
      {
        return (RogueClassValue*)(((RogueClassValue*)(RogueOptionalInt64__to_Value( (((ROGUEM107)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))))) ))));
      }
      case 199:
      {
        return (RogueClassValue*)(((RogueClassValue*)(RogueOptionalTableEntry_String_String___to_Value( (((ROGUEM117)(intptr_t)fn_0)(ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))))) ))));
      }
      case 200:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueString*)((ROGUEM141)(intptr_t)fn_0)((RogueOptionalTableEntry_String_String___from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))))))) )));
      }
      case 201:
      {
        return (RogueClassValue*)(((RogueClassValue*)((ROGUEM141)(intptr_t)fn_0)((RogueOptionalTableEntry_String_String___from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) )))));
      }
      case 202:
      {
        return (RogueClassValue*)(((RogueClassValue*)(RogueFileOptions__to_Value( (((ROGUEM109)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))))) ))));
      }
      case 203:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueClassOptionalBoxed_TableEntry_String_Value__*)((ROGUEM110)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))),(RogueOptionalTableEntry_String_Value___from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) ))))))) )));
      }
      case 204:
      {
        return (RogueClassValue*)(((RogueClassValue*)(RogueOptionalTableEntry_String_Value___to_Value( (((ROGUEM111)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))))) ))));
      }
      case 205:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueClassOptionalBoxed_TypeInfo_*)((ROGUEM112)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))),(RogueOptionalTypeInfo__from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) ))))))) )));
      }
      case 206:
      {
        return (RogueClassValue*)(((RogueClassValue*)(RogueOptionalTypeInfo__to_Value( (((ROGUEM113)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))))) ))));
      }
      case 207:
      {
        return (RogueClassValue*)(((RogueClassValue*)(RogueOptionalTableEntry_String_TypeInfo___to_Value( (((ROGUEM119)(intptr_t)fn_0)(ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))))) ))));
      }
      case 208:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueString*)((ROGUEM142)(intptr_t)fn_0)((RogueOptionalTableEntry_String_TypeInfo___from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))))))) )));
      }
      case 209:
      {
        return (RogueClassValue*)(((RogueClassValue*)((ROGUEM142)(intptr_t)fn_0)((RogueOptionalTableEntry_String_TypeInfo___from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) )))));
      }
      case 210:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueClassOptionalBoxed_TableEntry_Int32_String__*)((ROGUEM114)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))),(RogueOptionalTableEntry_Int32_String___from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) ))))))) )));
      }
      case 211:
      {
        return (RogueClassValue*)(((RogueClassValue*)(RogueOptionalTableEntry_Int32_String___to_Value( (((ROGUEM115)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))))) ))));
      }
      case 212:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueClassOptionalBoxed_TableEntry_String_String__*)((ROGUEM116)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))),(RogueOptionalTableEntry_String_String___from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) ))))))) )));
      }
      case 213:
      {
        return (RogueClassValue*)(((RogueClassValue*)(RogueOptionalTableEntry_String_String___to_Value( (((ROGUEM117)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))))) ))));
      }
      case 214:
      {
        return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueClassOptionalBoxed_TableEntry_String_TypeInfo__*)((ROGUEM118)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))),(RogueOptionalTableEntry_String_TypeInfo___from_value__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 1 ))) ))))))) )));
      }
      case 215:
      {
        return (RogueClassValue*)(((RogueClassValue*)(RogueOptionalTableEntry_String_TypeInfo___to_Value( (((ROGUEM119)(intptr_t)fn_0)(ROGUE_ARG(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM8( 32, args_2, 0 ))) ))))) ))));
      }
    }
  }
  return (RogueClassValue*)(((RogueClassValue*)(((RogueClassUndefinedValue*)ROGUE_SINGLETON(UndefinedValue)))));
}

void RogueMethodInfo__init_class()
{
  RogueMethodInfo__method_name_strings = ((RogueClassTable_Int32_String_*)(RogueTable_Int32_String___init( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassTable_Int32_String_*,ROGUE_CREATE_OBJECT(Table_Int32_String_))) )));
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
    throw ((RogueClassError*)Rogue_call_ROGUEM1( 16, ROGUE_ARG(((RogueClassError*)(((RogueException*)Rogue_call_ROGUEM3( 13, ROGUE_ARG(((RogueException*)ROGUE_CREATE_REF(RogueClassError*,ROGUE_CREATE_OBJECT(Error)))), Rogue_literal_strings[70] ))))) ));
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
    throw ((RogueClassIOError*)(RogueIOError___throw( ROGUE_ARG(((RogueClassIOError*)(((RogueException*)Rogue_call_ROGUEM3( 13, ROGUE_ARG(((RogueException*)ROGUE_CREATE_REF(RogueClassIOError*,ROGUE_CREATE_OBJECT(IOError)))), Rogue_literal_strings[14] ))))) )));
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
    ROGUE_DEF_LOCAL_REF(RogueString*,_auto_1226_0,(pattern_1));
    RogueInt32 i_0 = (((_auto_1226_0->character_count) - (1)));
    for (;ROGUE_COND(((i_0) >= (0)));--i_0)
    {
      ROGUE_GC_CHECK;
      RogueCharacter ch_0 = (RogueString_character_at(_auto_1226_0,i_0));
      if (ROGUE_COND(((((ch_0) == ((RogueCharacter)'*'))) || (((ch_0) == ((RogueCharacter)'?'))))))
      {
        last_wildcard_2 = ((RogueInt32)(i_0));
        goto _auto_1227;
      }
    }
  }
  _auto_1227:;
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
        ROGUE_DEF_LOCAL_REF(RogueString*,_auto_1228_0,(filepath_0));
        RogueInt32 _auto_1229_0 = (((filepath_0->character_count) - (end_count_3)));
        RogueInt32 _auto_1230_0 = (((_auto_1228_0->character_count) - (1)));
        for (;ROGUE_COND(((_auto_1229_0) <= (_auto_1230_0)));++_auto_1229_0)
        {
          ROGUE_GC_CHECK;
          RogueCharacter ch_0 = (RogueString_character_at(_auto_1228_0,_auto_1229_0));
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
    ROGUE_DEF_LOCAL_REF(RogueClassValueTable*,_auto_1257_0,(((RogueClassValueTable*)(RogueValueTable__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassValueTable*,ROGUE_CREATE_OBJECT(ValueTable))) )))));
    {
    }
    return (RogueClassValue*)(((RogueClassValue*)(_auto_1257_0)));
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

RogueString* RogueRuntime__literal_string__Int32( RogueInt32 string_index_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((((string_index_0) < (0))) || (((string_index_0) >= ((RogueRuntime__literal_string_count())))))))
  {
    return (RogueString*)(((RogueString*)(NULL)));
  }
  return (RogueString*)(((RogueString*)(Rogue_literal_strings[string_index_0])));
}

RogueInt32 RogueRuntime__literal_string_count()
{
  ROGUE_GC_CHECK;
  return (RogueInt32)(((RogueInt32)(Rogue_literal_string_count)));
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
      ROGUE_DEF_LOCAL_REF(RogueString*,_auto_1252_0,(value_0));
      RogueInt32 _auto_1253_0 = (0);
      RogueInt32 _auto_1254_0 = (((_auto_1252_0->character_count) - (1)));
      for (;ROGUE_COND(((_auto_1253_0) <= (_auto_1254_0)));++_auto_1253_0)
      {
        ROGUE_GC_CHECK;
        RogueCharacter ch_0 = (RogueString_character_at(_auto_1252_0,_auto_1253_0));
        switch (ch_0)
        {
          case (RogueCharacter)'"':
          {
            RogueStringBuilder__print__String( buffer_1, Rogue_literal_strings[187] );
            break;
          }
          case (RogueCharacter)'\\':
          {
            RogueStringBuilder__print__String( buffer_1, Rogue_literal_strings[188] );
            break;
          }
          case (RogueCharacter)8:
          {
            RogueStringBuilder__print__String( buffer_1, Rogue_literal_strings[189] );
            break;
          }
          case (RogueCharacter)12:
          {
            RogueStringBuilder__print__String( buffer_1, Rogue_literal_strings[190] );
            break;
          }
          case (RogueCharacter)10:
          {
            RogueStringBuilder__print__String( buffer_1, Rogue_literal_strings[191] );
            break;
          }
          case (RogueCharacter)13:
          {
            RogueStringBuilder__print__String( buffer_1, Rogue_literal_strings[192] );
            break;
          }
          case (RogueCharacter)9:
          {
            RogueStringBuilder__print__String( buffer_1, Rogue_literal_strings[193] );
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
              RogueStringBuilder__print__String( buffer_1, Rogue_literal_strings[194] );
              RogueInt32 n_3 = (((RogueInt32)(ch_0)));
              {
                RogueInt32 nibble_5 = (0);
                RogueInt32 _auto_330_6 = (3);
                for (;ROGUE_COND(((nibble_5) <= (_auto_330_6)));++nibble_5)
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

RogueOptionalInt32 RogueOptionalInt32__from_value__Value( RogueClassValue* value_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND((((Rogue_call_ROGUEM9( 47, value_0 ))) && (RogueObject_instance_of(((RogueObject*)Rogue_call_ROGUEM1( 111, value_0 )),RogueTypeOptionalBoxed_Int32_)))))
  {
    return (RogueOptionalInt32)(((RogueOptionalOptionalBoxed_Int32___to_Int32( ROGUE_ARG(((RogueClassOptionalBoxed_Int32_*)(RogueObject_as(((RogueObject*)Rogue_call_ROGUEM1( 111, value_0 )),RogueTypeOptionalBoxed_Int32_)))) ))));
  }
  return (RogueOptionalInt32)(RogueOptionalInt32( (RogueInt32__create__Real64( ROGUE_ARG((Rogue_call_ROGUEM13( 109, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM3( 33, value_0, Rogue_literal_strings[227] ))) ))) )), (RogueLogical__create__Real64( ROGUE_ARG((Rogue_call_ROGUEM13( 109, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM3( 33, value_0, Rogue_literal_strings[228] ))) ))) )) ));
}

RogueClassSystemEnvironment RogueSystemEnvironment__from_value__Value( RogueClassValue* value_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND((((Rogue_call_ROGUEM9( 47, value_0 ))) && (RogueObject_instance_of(((RogueObject*)Rogue_call_ROGUEM1( 111, value_0 )),RogueTypeBoxed_SystemEnvironment_)))))
  {
    return (RogueClassSystemEnvironment)(((RogueBoxed_SystemEnvironment___to_SystemEnvironment( ROGUE_ARG(((RogueClassBoxed_SystemEnvironment_*)(RogueObject_as(((RogueObject*)Rogue_call_ROGUEM1( 111, value_0 )),RogueTypeBoxed_SystemEnvironment_)))) ))));
  }
  return (RogueClassSystemEnvironment)(RogueClassSystemEnvironment());
}

RogueOptionalByte RogueOptionalByte__create()
{
  ROGUE_GC_CHECK;
  RogueByte default_value_0 = 0;
  return (RogueOptionalByte)(RogueOptionalByte( default_value_0, false ));
}

RogueOptionalByte RogueOptionalByte__from_value__Value( RogueClassValue* value_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND((((Rogue_call_ROGUEM9( 47, value_0 ))) && (RogueObject_instance_of(((RogueObject*)Rogue_call_ROGUEM1( 111, value_0 )),RogueTypeOptionalBoxed_Byte_)))))
  {
    return (RogueOptionalByte)(((RogueOptionalOptionalBoxed_Byte___to_Byte( ROGUE_ARG(((RogueClassOptionalBoxed_Byte_*)(RogueObject_as(((RogueObject*)Rogue_call_ROGUEM1( 111, value_0 )),RogueTypeOptionalBoxed_Byte_)))) ))));
  }
  return (RogueOptionalByte)(RogueOptionalByte( (RogueByte__create__Real64( ROGUE_ARG((Rogue_call_ROGUEM13( 109, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM3( 33, value_0, Rogue_literal_strings[227] ))) ))) )), (RogueLogical__create__Real64( ROGUE_ARG((Rogue_call_ROGUEM13( 109, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM3( 33, value_0, Rogue_literal_strings[228] ))) ))) )) ));
}

RogueOptionalValue RogueOptionalValue__from_value__Value( RogueClassValue* value_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND((((Rogue_call_ROGUEM9( 47, value_0 ))) && (RogueObject_instance_of(((RogueObject*)Rogue_call_ROGUEM1( 111, value_0 )),RogueTypeOptionalBoxed_Value_)))))
  {
    return (RogueOptionalValue)(((RogueOptionalOptionalBoxed_Value___to_Value( ROGUE_ARG(((RogueClassOptionalBoxed_Value_*)(RogueObject_as(((RogueObject*)Rogue_call_ROGUEM1( 111, value_0 )),RogueTypeOptionalBoxed_Value_)))) ))));
  }
  return (RogueOptionalValue)(RogueOptionalValue( ((RogueClassValue*)(RogueObject_as(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM3( 33, value_0, Rogue_literal_strings[227] ))) )),RogueTypeValue))), (RogueLogical__create__Real64( ROGUE_ARG((Rogue_call_ROGUEM13( 109, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM3( 33, value_0, Rogue_literal_strings[228] ))) ))) )) ));
}

RogueOptionalStringBuilder RogueOptionalStringBuilder__from_value__Value( RogueClassValue* value_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND((((Rogue_call_ROGUEM9( 47, value_0 ))) && (RogueObject_instance_of(((RogueObject*)Rogue_call_ROGUEM1( 111, value_0 )),RogueTypeOptionalBoxed_StringBuilder_)))))
  {
    return (RogueOptionalStringBuilder)(((RogueOptionalOptionalBoxed_StringBuilder___to_StringBuilder( ROGUE_ARG(((RogueClassOptionalBoxed_StringBuilder_*)(RogueObject_as(((RogueObject*)Rogue_call_ROGUEM1( 111, value_0 )),RogueTypeOptionalBoxed_StringBuilder_)))) ))));
  }
  return (RogueOptionalStringBuilder)(RogueOptionalStringBuilder( ((RogueStringBuilder*)(RogueObject_as(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM3( 33, value_0, Rogue_literal_strings[227] ))) )),RogueTypeStringBuilder))), (RogueLogical__create__Real64( ROGUE_ARG((Rogue_call_ROGUEM13( 109, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM3( 33, value_0, Rogue_literal_strings[228] ))) ))) )) ));
}

RogueOptional_Function___ RogueOptional_Function_____from_value__Value( RogueClassValue* value_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND((((Rogue_call_ROGUEM9( 47, value_0 ))) && (RogueObject_instance_of(((RogueObject*)Rogue_call_ROGUEM1( 111, value_0 )),RogueTypeOptionalBoxed__Function____)))))
  {
    return (RogueOptional_Function___)(((RogueOptionalOptionalBoxed__Function______to__Function___( ROGUE_ARG(((RogueClassOptionalBoxed__Function____*)(RogueObject_as(((RogueObject*)Rogue_call_ROGUEM1( 111, value_0 )),RogueTypeOptionalBoxed__Function____)))) ))));
  }
  return (RogueOptional_Function___)(RogueOptional_Function___( ((RogueClass_Function___*)(RogueObject_as(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM3( 33, value_0, Rogue_literal_strings[227] ))) )),RogueType_Function___))), (RogueLogical__create__Real64( ROGUE_ARG((Rogue_call_ROGUEM13( 109, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM3( 33, value_0, Rogue_literal_strings[228] ))) ))) )) ));
}

RogueOptionalString RogueOptionalString__from_value__Value( RogueClassValue* value_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND((((Rogue_call_ROGUEM9( 47, value_0 ))) && (RogueObject_instance_of(((RogueObject*)Rogue_call_ROGUEM1( 111, value_0 )),RogueTypeOptionalBoxed_String_)))))
  {
    return (RogueOptionalString)(((RogueOptionalOptionalBoxed_String___to_String( ROGUE_ARG(((RogueClassOptionalBoxed_String_*)(RogueObject_as(((RogueObject*)Rogue_call_ROGUEM1( 111, value_0 )),RogueTypeOptionalBoxed_String_)))) ))));
  }
  return (RogueOptionalString)(RogueOptionalString( ((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)((RogueClassValue*)Rogue_call_ROGUEM3( 33, value_0, Rogue_literal_strings[227] )))) )), (RogueLogical__create__Real64( ROGUE_ARG((Rogue_call_ROGUEM13( 109, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM3( 33, value_0, Rogue_literal_strings[228] ))) ))) )) ));
}

RogueOptionalPropertyInfo RogueOptionalPropertyInfo__from_value__Value( RogueClassValue* value_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND((((Rogue_call_ROGUEM9( 47, value_0 ))) && (RogueObject_instance_of(((RogueObject*)Rogue_call_ROGUEM1( 111, value_0 )),RogueTypeOptionalBoxed_PropertyInfo_)))))
  {
    return (RogueOptionalPropertyInfo)(((RogueOptionalOptionalBoxed_PropertyInfo___to_PropertyInfo( ROGUE_ARG(((RogueClassOptionalBoxed_PropertyInfo_*)(RogueObject_as(((RogueObject*)Rogue_call_ROGUEM1( 111, value_0 )),RogueTypeOptionalBoxed_PropertyInfo_)))) ))));
  }
  return (RogueOptionalPropertyInfo)(RogueOptionalPropertyInfo( ((RogueClassPropertyInfo*)(RogueObject_as(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM3( 33, value_0, Rogue_literal_strings[227] ))) )),RogueTypePropertyInfo))), (RogueLogical__create__Real64( ROGUE_ARG((Rogue_call_ROGUEM13( 109, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM3( 33, value_0, Rogue_literal_strings[228] ))) ))) )) ));
}

RogueOptionalMethodInfo RogueOptionalMethodInfo__from_value__Value( RogueClassValue* value_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND((((Rogue_call_ROGUEM9( 47, value_0 ))) && (RogueObject_instance_of(((RogueObject*)Rogue_call_ROGUEM1( 111, value_0 )),RogueTypeOptionalBoxed_MethodInfo_)))))
  {
    return (RogueOptionalMethodInfo)(((RogueOptionalOptionalBoxed_MethodInfo___to_MethodInfo( ROGUE_ARG(((RogueClassOptionalBoxed_MethodInfo_*)(RogueObject_as(((RogueObject*)Rogue_call_ROGUEM1( 111, value_0 )),RogueTypeOptionalBoxed_MethodInfo_)))) ))));
  }
  return (RogueOptionalMethodInfo)(RogueOptionalMethodInfo( ((RogueClassMethodInfo*)(RogueObject_as(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM3( 33, value_0, Rogue_literal_strings[227] ))) )),RogueTypeMethodInfo))), (RogueLogical__create__Real64( ROGUE_ARG((Rogue_call_ROGUEM13( 109, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM3( 33, value_0, Rogue_literal_strings[228] ))) ))) )) ));
}

RogueOptionalCharacter RogueOptionalCharacter__create()
{
  ROGUE_GC_CHECK;
  RogueCharacter default_value_0 = 0;
  return (RogueOptionalCharacter)(RogueOptionalCharacter( default_value_0, false ));
}

RogueOptionalCharacter RogueOptionalCharacter__from_value__Value( RogueClassValue* value_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND((((Rogue_call_ROGUEM9( 47, value_0 ))) && (RogueObject_instance_of(((RogueObject*)Rogue_call_ROGUEM1( 111, value_0 )),RogueTypeOptionalBoxed_Character_)))))
  {
    return (RogueOptionalCharacter)(((RogueOptionalOptionalBoxed_Character___to_Character( ROGUE_ARG(((RogueClassOptionalBoxed_Character_*)(RogueObject_as(((RogueObject*)Rogue_call_ROGUEM1( 111, value_0 )),RogueTypeOptionalBoxed_Character_)))) ))));
  }
  return (RogueOptionalCharacter)(RogueOptionalCharacter( (RogueCharacter__create__Real64( ROGUE_ARG((Rogue_call_ROGUEM13( 109, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM3( 33, value_0, Rogue_literal_strings[227] ))) ))) )), (RogueLogical__create__Real64( ROGUE_ARG((Rogue_call_ROGUEM13( 109, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM3( 33, value_0, Rogue_literal_strings[228] ))) ))) )) ));
}

RogueOptionalReal64 RogueOptionalReal64__from_value__Value( RogueClassValue* value_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND((((Rogue_call_ROGUEM9( 47, value_0 ))) && (RogueObject_instance_of(((RogueObject*)Rogue_call_ROGUEM1( 111, value_0 )),RogueTypeOptionalBoxed_Real64_)))))
  {
    return (RogueOptionalReal64)(((RogueOptionalOptionalBoxed_Real64___to_Real64( ROGUE_ARG(((RogueClassOptionalBoxed_Real64_*)(RogueObject_as(((RogueObject*)Rogue_call_ROGUEM1( 111, value_0 )),RogueTypeOptionalBoxed_Real64_)))) ))));
  }
  return (RogueOptionalReal64)(RogueOptionalReal64( (RogueReal64__create__Real64( ROGUE_ARG((Rogue_call_ROGUEM13( 109, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM3( 33, value_0, Rogue_literal_strings[227] ))) ))) )), (RogueLogical__create__Real64( ROGUE_ARG((Rogue_call_ROGUEM13( 109, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM3( 33, value_0, Rogue_literal_strings[228] ))) ))) )) ));
}

RogueOptionalReal32 RogueOptionalReal32__from_value__Value( RogueClassValue* value_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND((((Rogue_call_ROGUEM9( 47, value_0 ))) && (RogueObject_instance_of(((RogueObject*)Rogue_call_ROGUEM1( 111, value_0 )),RogueTypeOptionalBoxed_Real32_)))))
  {
    return (RogueOptionalReal32)(((RogueOptionalOptionalBoxed_Real32___to_Real32( ROGUE_ARG(((RogueClassOptionalBoxed_Real32_*)(RogueObject_as(((RogueObject*)Rogue_call_ROGUEM1( 111, value_0 )),RogueTypeOptionalBoxed_Real32_)))) ))));
  }
  return (RogueOptionalReal32)(RogueOptionalReal32( (RogueReal32__create__Real64( ROGUE_ARG((Rogue_call_ROGUEM13( 109, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM3( 33, value_0, Rogue_literal_strings[227] ))) ))) )), (RogueLogical__create__Real64( ROGUE_ARG((Rogue_call_ROGUEM13( 109, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM3( 33, value_0, Rogue_literal_strings[228] ))) ))) )) ));
}

RogueOptionalInt64 RogueOptionalInt64__from_value__Value( RogueClassValue* value_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND((((Rogue_call_ROGUEM9( 47, value_0 ))) && (RogueObject_instance_of(((RogueObject*)Rogue_call_ROGUEM1( 111, value_0 )),RogueTypeOptionalBoxed_Int64_)))))
  {
    return (RogueOptionalInt64)(((RogueOptionalOptionalBoxed_Int64___to_Int64( ROGUE_ARG(((RogueClassOptionalBoxed_Int64_*)(RogueObject_as(((RogueObject*)Rogue_call_ROGUEM1( 111, value_0 )),RogueTypeOptionalBoxed_Int64_)))) ))));
  }
  return (RogueOptionalInt64)(RogueOptionalInt64( (RogueInt64__create__Real64( ROGUE_ARG((Rogue_call_ROGUEM13( 109, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM3( 33, value_0, Rogue_literal_strings[227] ))) ))) )), (RogueLogical__create__Real64( ROGUE_ARG((Rogue_call_ROGUEM13( 109, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM3( 33, value_0, Rogue_literal_strings[228] ))) ))) )) ));
}

RogueClassFileOptions RogueFileOptions__from_value__Value( RogueClassValue* value_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND((((Rogue_call_ROGUEM9( 47, value_0 ))) && (RogueObject_instance_of(((RogueObject*)Rogue_call_ROGUEM1( 111, value_0 )),RogueTypeBoxed_FileOptions_)))))
  {
    return (RogueClassFileOptions)(((RogueBoxed_FileOptions___to_FileOptions( ROGUE_ARG(((RogueClassBoxed_FileOptions_*)(RogueObject_as(((RogueObject*)Rogue_call_ROGUEM1( 111, value_0 )),RogueTypeBoxed_FileOptions_)))) ))));
  }
  return (RogueClassFileOptions)(RogueClassFileOptions( (RogueInt32__create__Real64( ROGUE_ARG((Rogue_call_ROGUEM13( 109, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM3( 33, value_0, Rogue_literal_strings[254] ))) ))) )) ));
}

RogueOptionalTableEntry_String_Value_ RogueOptionalTableEntry_String_Value___from_value__Value( RogueClassValue* value_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND((((Rogue_call_ROGUEM9( 47, value_0 ))) && (RogueObject_instance_of(((RogueObject*)Rogue_call_ROGUEM1( 111, value_0 )),RogueTypeOptionalBoxed_TableEntry_String_Value__)))))
  {
    return (RogueOptionalTableEntry_String_Value_)(((RogueOptionalOptionalBoxed_TableEntry_String_Value____to_TableEntry_String_Value_( ROGUE_ARG(((RogueClassOptionalBoxed_TableEntry_String_Value__*)(RogueObject_as(((RogueObject*)Rogue_call_ROGUEM1( 111, value_0 )),RogueTypeOptionalBoxed_TableEntry_String_Value__)))) ))));
  }
  return (RogueOptionalTableEntry_String_Value_)(RogueOptionalTableEntry_String_Value_( ((RogueClassTableEntry_String_Value_*)(RogueObject_as(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM3( 33, value_0, Rogue_literal_strings[227] ))) )),RogueTypeTableEntry_String_Value_))), (RogueLogical__create__Real64( ROGUE_ARG((Rogue_call_ROGUEM13( 109, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM3( 33, value_0, Rogue_literal_strings[228] ))) ))) )) ));
}

RogueOptionalTypeInfo RogueOptionalTypeInfo__from_value__Value( RogueClassValue* value_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND((((Rogue_call_ROGUEM9( 47, value_0 ))) && (RogueObject_instance_of(((RogueObject*)Rogue_call_ROGUEM1( 111, value_0 )),RogueTypeOptionalBoxed_TypeInfo_)))))
  {
    return (RogueOptionalTypeInfo)(((RogueOptionalOptionalBoxed_TypeInfo___to_TypeInfo( ROGUE_ARG(((RogueClassOptionalBoxed_TypeInfo_*)(RogueObject_as(((RogueObject*)Rogue_call_ROGUEM1( 111, value_0 )),RogueTypeOptionalBoxed_TypeInfo_)))) ))));
  }
  return (RogueOptionalTypeInfo)(RogueOptionalTypeInfo( ((RogueTypeInfo*)(RogueObject_as(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM3( 33, value_0, Rogue_literal_strings[227] ))) )),RogueTypeTypeInfo))), (RogueLogical__create__Real64( ROGUE_ARG((Rogue_call_ROGUEM13( 109, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM3( 33, value_0, Rogue_literal_strings[228] ))) ))) )) ));
}

RogueOptionalTableEntry_Int32_String_ RogueOptionalTableEntry_Int32_String___from_value__Value( RogueClassValue* value_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND((((Rogue_call_ROGUEM9( 47, value_0 ))) && (RogueObject_instance_of(((RogueObject*)Rogue_call_ROGUEM1( 111, value_0 )),RogueTypeOptionalBoxed_TableEntry_Int32_String__)))))
  {
    return (RogueOptionalTableEntry_Int32_String_)(((RogueOptionalOptionalBoxed_TableEntry_Int32_String____to_TableEntry_Int32_String_( ROGUE_ARG(((RogueClassOptionalBoxed_TableEntry_Int32_String__*)(RogueObject_as(((RogueObject*)Rogue_call_ROGUEM1( 111, value_0 )),RogueTypeOptionalBoxed_TableEntry_Int32_String__)))) ))));
  }
  return (RogueOptionalTableEntry_Int32_String_)(RogueOptionalTableEntry_Int32_String_( ((RogueClassTableEntry_Int32_String_*)(RogueObject_as(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM3( 33, value_0, Rogue_literal_strings[227] ))) )),RogueTypeTableEntry_Int32_String_))), (RogueLogical__create__Real64( ROGUE_ARG((Rogue_call_ROGUEM13( 109, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM3( 33, value_0, Rogue_literal_strings[228] ))) ))) )) ));
}

RogueOptionalTableEntry_String_String_ RogueOptionalTableEntry_String_String___from_value__Value( RogueClassValue* value_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND((((Rogue_call_ROGUEM9( 47, value_0 ))) && (RogueObject_instance_of(((RogueObject*)Rogue_call_ROGUEM1( 111, value_0 )),RogueTypeOptionalBoxed_TableEntry_String_String__)))))
  {
    return (RogueOptionalTableEntry_String_String_)(((RogueOptionalOptionalBoxed_TableEntry_String_String____to_TableEntry_String_String_( ROGUE_ARG(((RogueClassOptionalBoxed_TableEntry_String_String__*)(RogueObject_as(((RogueObject*)Rogue_call_ROGUEM1( 111, value_0 )),RogueTypeOptionalBoxed_TableEntry_String_String__)))) ))));
  }
  return (RogueOptionalTableEntry_String_String_)(RogueOptionalTableEntry_String_String_( ((RogueClassTableEntry_String_String_*)(RogueObject_as(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM3( 33, value_0, Rogue_literal_strings[227] ))) )),RogueTypeTableEntry_String_String_))), (RogueLogical__create__Real64( ROGUE_ARG((Rogue_call_ROGUEM13( 109, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM3( 33, value_0, Rogue_literal_strings[228] ))) ))) )) ));
}

RogueOptionalTableEntry_String_TypeInfo_ RogueOptionalTableEntry_String_TypeInfo___from_value__Value( RogueClassValue* value_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND((((Rogue_call_ROGUEM9( 47, value_0 ))) && (RogueObject_instance_of(((RogueObject*)Rogue_call_ROGUEM1( 111, value_0 )),RogueTypeOptionalBoxed_TableEntry_String_TypeInfo__)))))
  {
    return (RogueOptionalTableEntry_String_TypeInfo_)(((RogueOptionalOptionalBoxed_TableEntry_String_TypeInfo____to_TableEntry_String_TypeInfo_( ROGUE_ARG(((RogueClassOptionalBoxed_TableEntry_String_TypeInfo__*)(RogueObject_as(((RogueObject*)Rogue_call_ROGUEM1( 111, value_0 )),RogueTypeOptionalBoxed_TableEntry_String_TypeInfo__)))) ))));
  }
  return (RogueOptionalTableEntry_String_TypeInfo_)(RogueOptionalTableEntry_String_TypeInfo_( ((RogueClassTableEntry_String_TypeInfo_*)(RogueObject_as(((RogueObject*)Rogue_call_ROGUEM1( 111, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM3( 33, value_0, Rogue_literal_strings[227] ))) )),RogueTypeTableEntry_String_TypeInfo_))), (RogueLogical__create__Real64( ROGUE_ARG((Rogue_call_ROGUEM13( 109, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM3( 33, value_0, Rogue_literal_strings[228] ))) ))) )) ));
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
  return (RogueString*)(Rogue_literal_strings[138]);
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
          RogueStringBuilder__print__String( quoted_1, Rogue_literal_strings[187] );
          break;
        }
        case (RogueCharacter)'\\':
        {
          RogueStringBuilder__print__String( quoted_1, Rogue_literal_strings[188] );
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
    throw ((RogueClassError*)Rogue_call_ROGUEM1( 16, ROGUE_ARG(((RogueClassError*)(((RogueException*)Rogue_call_ROGUEM3( 13, ROGUE_ARG(((RogueException*)ROGUE_CREATE_REF(RogueClassError*,ROGUE_CREATE_OBJECT(Error)))), ROGUE_ARG(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], Rogue_literal_strings[75] ))) )))), Rogue_literal_strings[76] )))), ROGUE_ARG(((RogueString*)(RogueString__operatorPLUS__Int32( ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], Rogue_literal_strings[0] ))), 140 )))) )))), Rogue_literal_strings[77] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], ROGUE_ARG(((RogueString*)(RogueString__to_lowercase( ROGUE_ARG(((((_auto_0_0))) ? (ROGUE_ARG(_auto_0_0)) : ROGUE_ARG(Rogue_literal_strings[0]))) )))) ))) )))), Rogue_literal_strings[78] )))) )))) ))))) ));
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
  if (ROGUE_COND((RogueOptionalValue__operator__Value( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM3( 33, ROGUE_ARG(THIS->cache), Rogue_literal_strings[116] ))) ))))
  {
    return;
  }
  if (ROGUE_COND(((0) == ((RogueSystem__run__String( Rogue_literal_strings[117] ))))))
  {
    Rogue_call_ROGUEM5( 101, ROGUE_ARG(THIS->cache), Rogue_literal_strings[116], ROGUE_ARG((RogueValue__create__Logical( true ))) );
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
      Rogue_call_ROGUEM5( 101, ROGUE_ARG(THIS->cache), Rogue_literal_strings[116], ROGUE_ARG((RogueValue__create__Logical( true ))) );
      RogueGlobal__save_cache( ROGUE_ARG(THIS) );
      return;
    }
    throw ((RogueClassError*)Rogue_call_ROGUEM1( 16, ROGUE_ARG(((RogueClassError*)(((RogueException*)Rogue_call_ROGUEM3( 13, ROGUE_ARG(((RogueException*)ROGUE_CREATE_REF(RogueClassError*,ROGUE_CREATE_OBJECT(Error)))), Rogue_literal_strings[120] ))))) ));
  }
  throw ((RogueClassError*)Rogue_call_ROGUEM1( 16, ROGUE_ARG(((RogueClassError*)(((RogueException*)Rogue_call_ROGUEM3( 13, ROGUE_ARG(((RogueException*)ROGUE_CREATE_REF(RogueClassError*,ROGUE_CREATE_OBJECT(Error)))), Rogue_literal_strings[121] ))))) ));
}

void RogueGlobal__install_library__Value( RogueClassGlobal* THIS, RogueClassValue* library_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND((Rogue_call_ROGUEM6( 20, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM3( 33, ROGUE_ARG(THIS->cache), Rogue_literal_strings[62] ))), ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM3( 33, library_0, Rogue_literal_strings[59] ))) ))))
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
      throw ((RogueClassError*)Rogue_call_ROGUEM1( 16, ROGUE_ARG(((RogueClassError*)(((RogueException*)Rogue_call_ROGUEM3( 13, ROGUE_ARG(((RogueException*)ROGUE_CREATE_REF(RogueClassError*,ROGUE_CREATE_OBJECT(Error)))), ROGUE_ARG(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[106] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], ROGUE_ARG((RogueSystem__os())) ))) )))), Rogue_literal_strings[33] )))) )))) ))))) ));
    }
  }
}

void RogueGlobal__install_macos_library__Value( RogueClassGlobal* THIS, RogueClassValue* library_0 )
{
  ROGUE_GC_CHECK;
  ROGUE_DEF_LOCAL_REF(RogueString*,library_name_1,(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)((RogueClassValue*)Rogue_call_ROGUEM3( 33, library_0, Rogue_literal_strings[59] )))) ))));
  RogueLogical performed_install_2 = (false);
  if (ROGUE_COND(((0) != ((RogueSystem__run__String( ROGUE_ARG(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[71] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], library_name_1 ))) )))), Rogue_literal_strings[72] )))) )))) ))))))
  {
    RogueGlobal__require_command_line( ROGUE_ARG(THIS) );
    if (ROGUE_COND(!(((RogueString__begins_with__Character( ROGUE_ARG(((RogueString*)(RogueString__to_lowercase( ROGUE_ARG((RogueConsole__input__String( ROGUE_ARG(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[79] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], library_name_1 ))) )))), Rogue_literal_strings[80] )))) )))) ))) )))), (RogueCharacter)'y' ))))))
    {
      throw ((RogueClassError*)Rogue_call_ROGUEM1( 16, ROGUE_ARG(((RogueClassError*)(((RogueException*)Rogue_call_ROGUEM3( 13, ROGUE_ARG(((RogueException*)ROGUE_CREATE_REF(RogueClassError*,ROGUE_CREATE_OBJECT(Error)))), ROGUE_ARG(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[81] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], library_name_1 ))) )))), Rogue_literal_strings[82] )))) )))) ))))) ));
    }
    ROGUE_DEF_LOCAL_REF(RogueClassValue*,install_cmd_3,(((RogueClassValue*)Rogue_call_ROGUEM3( 33, library_0, Rogue_literal_strings[59] ))));
    if (ROGUE_COND((Rogue_call_ROGUEM6( 19, library_0, Rogue_literal_strings[83] ))))
    {
      install_cmd_3 = ((RogueClassValue*)(((RogueClassValue*)Rogue_call_ROGUEM3( 33, library_0, Rogue_literal_strings[83] ))));
    }
    ROGUE_DEF_LOCAL_REF(RogueString*,cmd_4,(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[84] )))), ROGUE_ARG(((RogueString*)(RogueString__operatorPLUS__Object( ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], Rogue_literal_strings[0] ))), ROGUE_ARG(((RogueObject*)(install_cmd_3))) )))) )))) )))));
    RogueGlobal__println__String( ROGUE_ARG(THIS), cmd_4 );
    if (ROGUE_COND(((0) != ((RogueSystem__run__String( cmd_4 ))))))
    {
      throw ((RogueClassError*)Rogue_call_ROGUEM1( 16, ROGUE_ARG(((RogueClassError*)(((RogueException*)Rogue_call_ROGUEM3( 13, ROGUE_ARG(((RogueException*)ROGUE_CREATE_REF(RogueClassError*,ROGUE_CREATE_OBJECT(Error)))), ROGUE_ARG(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[85] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], library_name_1 ))) )))), Rogue_literal_strings[82] )))) )))) ))))) ));
    }
    performed_install_2 = ((RogueLogical)(true));
  }
  if (ROGUE_COND(!(performed_install_2)))
  {
    RogueGlobal__println__String( ROGUE_ARG(THIS), ROGUE_ARG(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[86] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], library_name_1 ))) )))), Rogue_literal_strings[87] )))) )))) );
  }
  if (ROGUE_COND(((0) != ((RogueSystem__run__String( ROGUE_ARG(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[71] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], library_name_1 ))) )))), Rogue_literal_strings[88] )))) )))) ))))))
  {
    throw ((RogueClassError*)Rogue_call_ROGUEM1( 16, ROGUE_ARG(((RogueClassError*)(((RogueException*)Rogue_call_ROGUEM3( 13, ROGUE_ARG(((RogueException*)ROGUE_CREATE_REF(RogueClassError*,ROGUE_CREATE_OBJECT(Error)))), ROGUE_ARG(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[89] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], library_name_1 ))) )))), Rogue_literal_strings[82] )))) )))) ))))) ));
  }
  ROGUE_DEF_LOCAL_REF(RogueString*,header_path_5,0);
  ROGUE_DEF_LOCAL_REF(RogueString*,library_path_6,0);
  if (ROGUE_COND((Rogue_call_ROGUEM6( 19, library_0, Rogue_literal_strings[90] ))))
  {
    header_path_5 = ((RogueString*)(((RogueString*)(RogueGlobal__find_path__Value_String( ROGUE_ARG(THIS), library_0, ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)((RogueClassValue*)Rogue_call_ROGUEM3( 33, library_0, Rogue_literal_strings[90] )))) ))) )))));
  }
  if (ROGUE_COND((Rogue_call_ROGUEM6( 19, library_0, Rogue_literal_strings[93] ))))
  {
    library_path_6 = ((RogueString*)(((RogueString*)(RogueGlobal__find_path__Value_String( ROGUE_ARG(THIS), library_0, ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)((RogueClassValue*)Rogue_call_ROGUEM3( 33, library_0, Rogue_literal_strings[93] )))) ))) )))));
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
    RogueValueTable__set__String_Value( ROGUE_ARG(((RogueClassValueTable*)Rogue_call_ROGUEM3( 29, ROGUE_ARG(THIS->cache), Rogue_literal_strings[62] ))), library_name_1, ROGUE_ARG((RogueValue__create__Logical( true ))) );
    RogueValueTable__set__String_Value( ROGUE_ARG(((RogueClassValueTable*)Rogue_call_ROGUEM3( 29, ROGUE_ARG(THIS->cache), Rogue_literal_strings[100] ))), library_name_1, ROGUE_ARG((RogueValue__create__String( header_path_5 ))) );
    RogueValueTable__set__String_Value( ROGUE_ARG(((RogueClassValueTable*)Rogue_call_ROGUEM3( 29, ROGUE_ARG(THIS->cache), Rogue_literal_strings[101] ))), library_name_1, ROGUE_ARG((RogueValue__create__String( library_path_6 ))) );
    RogueGlobal__save_cache( ROGUE_ARG(THIS) );
    return;
  }
  throw ((RogueClassError*)Rogue_call_ROGUEM1( 16, ROGUE_ARG(((RogueClassError*)(((RogueException*)Rogue_call_ROGUEM3( 13, ROGUE_ARG(((RogueException*)ROGUE_CREATE_REF(RogueClassError*,ROGUE_CREATE_OBJECT(Error)))), ROGUE_ARG(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[102] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], library_name_1 ))) )))), Rogue_literal_strings[82] )))) )))) ))))) ));
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
  ROGUE_DEF_LOCAL_REF(RogueString*,library_name_1,(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)((RogueClassValue*)Rogue_call_ROGUEM3( 33, library_0, Rogue_literal_strings[59] )))) ))));
  ROGUE_DEF_LOCAL_REF(RogueString*,cmd_2,(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[103] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], library_name_1 ))) )))), Rogue_literal_strings[72] )))) )))));
  RogueGlobal__println__String( ROGUE_ARG(THIS), ROGUE_ARG(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[104] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], library_name_1 ))) )))), Rogue_literal_strings[54] )))) )))) );
  RogueGlobal__flush( ROGUE_ARG(((RogueClassGlobal*)(RogueGlobal__println__String( ROGUE_ARG(THIS), cmd_2 )))) );
  if (ROGUE_COND(((0) != ((RogueSystem__run__String( cmd_2 ))))))
  {
    RogueGlobal__require_command_line( ROGUE_ARG(THIS) );
    if (ROGUE_COND(!(((RogueString__begins_with__Character( ROGUE_ARG(((RogueString*)(RogueString__to_lowercase( ROGUE_ARG((RogueConsole__input__String( ROGUE_ARG(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[79] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], library_name_1 ))) )))), Rogue_literal_strings[80] )))) )))) ))) )))), (RogueCharacter)'y' ))))))
    {
      throw ((RogueClassError*)Rogue_call_ROGUEM1( 16, ROGUE_ARG(((RogueClassError*)(((RogueException*)Rogue_call_ROGUEM3( 13, ROGUE_ARG(((RogueException*)ROGUE_CREATE_REF(RogueClassError*,ROGUE_CREATE_OBJECT(Error)))), ROGUE_ARG(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[81] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], library_name_1 ))) )))), Rogue_literal_strings[82] )))) )))) ))))) ));
    }
    ROGUE_DEF_LOCAL_REF(RogueClassValue*,install_cmd_3,(((RogueClassValue*)Rogue_call_ROGUEM3( 33, library_0, Rogue_literal_strings[59] ))));
    if (ROGUE_COND((Rogue_call_ROGUEM6( 19, library_0, Rogue_literal_strings[83] ))))
    {
      install_cmd_3 = ((RogueClassValue*)(((RogueClassValue*)Rogue_call_ROGUEM3( 33, library_0, Rogue_literal_strings[83] ))));
    }
    cmd_2 = ((RogueString*)(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[105] )))), ROGUE_ARG(((RogueString*)(RogueString__operatorPLUS__Object( ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], Rogue_literal_strings[0] ))), ROGUE_ARG(((RogueObject*)(install_cmd_3))) )))) )))) )))));
    RogueGlobal__println__String( ROGUE_ARG(THIS), cmd_2 );
    if (ROGUE_COND(((0) != ((RogueSystem__run__String( cmd_2 ))))))
    {
      throw ((RogueClassError*)Rogue_call_ROGUEM1( 16, ROGUE_ARG(((RogueClassError*)(((RogueException*)Rogue_call_ROGUEM3( 13, ROGUE_ARG(((RogueException*)ROGUE_CREATE_REF(RogueClassError*,ROGUE_CREATE_OBJECT(Error)))), ROGUE_ARG(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[85] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], library_name_1 ))) )))), Rogue_literal_strings[82] )))) )))) ))))) ));
    }
  }
  RogueValueTable__set__String_Value( ROGUE_ARG(((RogueClassValueTable*)Rogue_call_ROGUEM3( 29, ROGUE_ARG(THIS->cache), Rogue_literal_strings[62] ))), library_name_1, ROGUE_ARG((RogueValue__create__Logical( true ))) );
  RogueGlobal__save_cache( ROGUE_ARG(THIS) );
}

RogueString* RogueGlobal__library_location__Value( RogueClassGlobal* THIS, RogueClassValue* library_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(!((Rogue_call_ROGUEM6( 20, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM3( 33, ROGUE_ARG(THIS->cache), Rogue_literal_strings[62] ))), ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM3( 33, library_0, Rogue_literal_strings[59] ))) )))))
  {
    RogueGlobal__install_library__Value( ROGUE_ARG(THIS), library_0 );
  }
  ROGUE_DEF_LOCAL_REF(RogueClassValue*,path_1,(((RogueClassValue*)Rogue_call_ROGUEM3( 33, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM3( 33, ROGUE_ARG(THIS->cache), Rogue_literal_strings[101] ))), ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)((RogueClassValue*)Rogue_call_ROGUEM3( 33, library_0, Rogue_literal_strings[59] )))) ))) ))));
  if (ROGUE_COND((RogueFile__exists__String( ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ((RogueObject*)path_1) ))) ))))
  {
    return (RogueString*)(((RogueString*)Rogue_call_ROGUEM1( 6, ((RogueObject*)path_1) )));
  }
  RogueGlobal__println__String( ROGUE_ARG(THIS), ROGUE_ARG(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[86] )))), ROGUE_ARG(((RogueString*)(RogueString__operatorPLUS__Object( ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], Rogue_literal_strings[0] ))), ROGUE_ARG(((RogueObject*)(((RogueClassValue*)Rogue_call_ROGUEM3( 33, library_0, Rogue_literal_strings[59] ))))) )))) )))), Rogue_literal_strings[87] )))) )))) );
  Rogue_call_ROGUEM3( 91, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM3( 33, ROGUE_ARG(THIS->cache), Rogue_literal_strings[62] ))), ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM3( 33, library_0, Rogue_literal_strings[59] ))) );
  RogueGlobal__save_cache( ROGUE_ARG(THIS) );
  return (RogueString*)(((RogueString*)(RogueGlobal__library_location__Value( ROGUE_ARG(THIS), library_0 ))));
}

RogueString* RogueGlobal__header_location__Value( RogueClassGlobal* THIS, RogueClassValue* library_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(!((Rogue_call_ROGUEM6( 20, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM3( 33, ROGUE_ARG(THIS->cache), Rogue_literal_strings[62] ))), ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM3( 33, library_0, Rogue_literal_strings[59] ))) )))))
  {
    RogueGlobal__install_library__Value( ROGUE_ARG(THIS), library_0 );
  }
  ROGUE_DEF_LOCAL_REF(RogueClassValue*,path_1,(((RogueClassValue*)Rogue_call_ROGUEM3( 33, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM3( 33, ROGUE_ARG(THIS->cache), Rogue_literal_strings[100] ))), ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)((RogueClassValue*)Rogue_call_ROGUEM3( 33, library_0, Rogue_literal_strings[59] )))) ))) ))));
  if (ROGUE_COND((RogueFile__exists__String( ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ((RogueObject*)path_1) ))) ))))
  {
    return (RogueString*)(((RogueString*)Rogue_call_ROGUEM1( 6, ((RogueObject*)path_1) )));
  }
  RogueGlobal__println__String( ROGUE_ARG(THIS), ROGUE_ARG(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[86] )))), ROGUE_ARG(((RogueString*)(RogueString__operatorPLUS__Object( ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], Rogue_literal_strings[0] ))), ROGUE_ARG(((RogueObject*)(((RogueClassValue*)Rogue_call_ROGUEM3( 33, library_0, Rogue_literal_strings[59] ))))) )))) )))), Rogue_literal_strings[87] )))) )))) );
  Rogue_call_ROGUEM3( 91, ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM3( 33, ROGUE_ARG(THIS->cache), Rogue_literal_strings[62] ))), ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM3( 33, library_0, Rogue_literal_strings[59] ))) );
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
          ROGUE_DEF_LOCAL_REF(RogueString*,_auto_259_0,(((RogueString*)(RogueString__before_first__Character( cmd_3, (RogueCharacter)'(' )))));
          if (ROGUE_COND((((RogueString__operatorEQUALSEQUALS__String_String( _auto_259_0, Rogue_literal_strings[56] ))) || ((RogueString__operatorEQUALSEQUALS__String_String( _auto_259_0, Rogue_literal_strings[16] ))))))
          {
            if (ROGUE_COND(((RogueString__begins_with__Character( args_4, (RogueCharacter)'"' )))))
            {
              args_4 = ((RogueString*)(((RogueString*)(RogueString__before_last__Character( ROGUE_ARG(((RogueString*)(RogueString__after_first__Character( args_4, (RogueCharacter)'"' )))), (RogueCharacter)'"' )))));
            }
            RogueValueTable__set__String_Value( ROGUE_ARG(THIS->config), cmd_3, ROGUE_ARG((RogueValue__create__String( args_4 ))) );
          }
          else if (ROGUE_COND((RogueString__operatorEQUALSEQUALS__String_String( _auto_259_0, Rogue_literal_strings[57] ))))
          {
            if (ROGUE_COND(!!(args_4->character_count)))
            {
              ROGUE_DEF_LOCAL_REF(RogueClassJSONParser*,parser_5,(((RogueClassJSONParser*)(RogueJSONParser__init__Scanner( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassJSONParser*,ROGUE_CREATE_OBJECT(JSONParser))), ROGUE_ARG(((RogueClassScanner*)(RogueScanner__init__String_Int32_Logical( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassScanner*,ROGUE_CREATE_OBJECT(Scanner))), args_4, 0, false )))) )))));
              RogueJSONParser__consume_spaces( parser_5 );
              while (ROGUE_COND(((RogueJSONParser__has_another( parser_5 )))))
              {
                ROGUE_GC_CHECK;
                ROGUE_DEF_LOCAL_REF(RogueString*,name_6,(((RogueString*)(RogueGlobal__parse_filepath__JSONParser( ROGUE_ARG(THIS), parser_5 )))));
                ROGUE_DEF_LOCAL_REF(RogueClassValueTable*,_auto_256_0,(((RogueClassValueTable*)(RogueValueTable__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassValueTable*,ROGUE_CREATE_OBJECT(ValueTable))) )))));
                {
                  RogueValueTable__set__String_Value( _auto_256_0, Rogue_literal_strings[59], ROGUE_ARG((RogueValue__create__String( name_6 ))) );
                }
                ROGUE_DEF_LOCAL_REF(RogueClassValueTable*,entry_7,(_auto_256_0));
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
                      goto _auto_258;
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
                  _auto_258:;
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
                ROGUE_DEF_LOCAL_REF(RogueClassValue*,_auto_260_0,(((RogueClassValue*)(RogueValueTable__get__String( ROGUE_ARG(THIS->config), Rogue_literal_strings[57] )))));
                RogueInt32 _auto_261_0 = (0);
                RogueInt32 _auto_262_0 = ((((Rogue_call_ROGUEM7( 22, _auto_260_0 ))) - (1)));
                for (;ROGUE_COND(((_auto_261_0) <= (_auto_262_0)));++_auto_261_0)
                {
                  ROGUE_GC_CHECK;
                  ROGUE_DEF_LOCAL_REF(RogueClassValue*,lib_0,(((RogueClassValue*)Rogue_call_ROGUEM8( 32, _auto_260_0, _auto_261_0 ))));
                  if (ROGUE_COND(((RogueValue__operatorEQUALSEQUALS__String( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM3( 33, lib_0, Rogue_literal_strings[59] ))), library_name_12 )))))
                  {
                    library_13 = ((RogueClassValue*)(lib_0));
                    goto _auto_263;
                  }
                }
              }
              ROGUE_DEF_LOCAL_REF(RogueClassValueTable*,_auto_264_0,(((RogueClassValueTable*)(RogueValueTable__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassValueTable*,ROGUE_CREATE_OBJECT(ValueTable))) )))));
              {
                RogueValueTable__set__String_Value( _auto_264_0, Rogue_literal_strings[59], ROGUE_ARG((RogueValue__create__String( library_name_12 ))) );
              }
              library_13 = ((RogueClassValue*)(((RogueClassValue*)(_auto_264_0))));
            }
            _auto_263:;
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
                ROGUE_DEF_LOCAL_REF(RogueClassValue*,_auto_265_0,(((RogueClassValue*)(RogueValueTable__get__String( ROGUE_ARG(THIS->config), Rogue_literal_strings[57] )))));
                RogueInt32 _auto_266_0 = (0);
                RogueInt32 _auto_267_0 = ((((Rogue_call_ROGUEM7( 22, _auto_265_0 ))) - (1)));
                for (;ROGUE_COND(((_auto_266_0) <= (_auto_267_0)));++_auto_266_0)
                {
                  ROGUE_GC_CHECK;
                  ROGUE_DEF_LOCAL_REF(RogueClassValue*,lib_0,(((RogueClassValue*)Rogue_call_ROGUEM8( 32, _auto_265_0, _auto_266_0 ))));
                  if (ROGUE_COND(((RogueValue__operatorEQUALSEQUALS__String( ROGUE_ARG(((RogueClassValue*)Rogue_call_ROGUEM3( 33, lib_0, Rogue_literal_strings[59] ))), library_name_15 )))))
                  {
                    library_16 = ((RogueClassValue*)(lib_0));
                    goto _auto_268;
                  }
                }
              }
              ROGUE_DEF_LOCAL_REF(RogueClassValueTable*,_auto_269_0,(((RogueClassValueTable*)(RogueValueTable__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassValueTable*,ROGUE_CREATE_OBJECT(ValueTable))) )))));
              {
                RogueValueTable__set__String_Value( _auto_269_0, Rogue_literal_strings[59], ROGUE_ARG((RogueValue__create__String( library_name_15 ))) );
              }
              library_16 = ((RogueClassValue*)(((RogueClassValue*)(_auto_269_0))));
            }
            _auto_268:;
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
  ROGUE_DEF_LOCAL_REF(RogueClassValueTable*,_auto_270_0,(((RogueClassValueTable*)(RogueValueTable__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassValueTable*,ROGUE_CREATE_OBJECT(ValueTable))) )))));
  {
    RogueValueTable__set__String_Value( _auto_270_0, Rogue_literal_strings[16], ROGUE_ARG((RogueValue__create__String( Rogue_literal_strings[16] ))) );
    RogueValueTable__set__String_Value( _auto_270_0, Rogue_literal_strings[17], ROGUE_ARG((RogueValue__create__String( Rogue_literal_strings[18] ))) );
  }
  ((RogueClassGlobal*)ROGUE_SINGLETON(Global))->config = _auto_270_0;
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
    ROGUE_DEF_LOCAL_REF(RogueString_List*,_auto_271_0,(RogueSystem_command_line_arguments));
    RogueInt32 _auto_272_0 = (0);
    RogueInt32 _auto_273_0 = (((_auto_271_0->count) - (1)));
    for (;ROGUE_COND(((_auto_272_0) <= (_auto_273_0)));++_auto_272_0)
    {
      ROGUE_GC_CHECK;
      ROGUE_DEF_LOCAL_REF(RogueString*,arg_0,(((RogueString*)(_auto_271_0->data->as_objects[_auto_272_0]))));
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
        goto _auto_274;
      }
    }
  }
  _auto_274:;
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
      ROGUE_DEF_LOCAL_REF(RogueString_List*,_auto_275_0,(((RogueString_List*)(RogueValueTable__keys( ROGUE_ARG(THIS->config) )))));
      RogueInt32 _auto_276_0 = (0);
      RogueInt32 _auto_277_0 = (((_auto_275_0->count) - (1)));
      for (;ROGUE_COND(((_auto_276_0) <= (_auto_277_0)));++_auto_276_0)
      {
        ROGUE_GC_CHECK;
        ROGUE_DEF_LOCAL_REF(RogueString*,key_0,(((RogueString*)(_auto_275_0->data->as_objects[_auto_276_0]))));
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
      ROGUE_DEF_LOCAL_REF(RogueClassValue*,_auto_278_0,(((RogueClassValue*)(RogueValueTable__get__String( ROGUE_ARG(THIS->config), Rogue_literal_strings[57] )))));
      RogueInt32 _auto_279_0 = (0);
      RogueInt32 _auto_280_0 = ((((Rogue_call_ROGUEM7( 22, _auto_278_0 ))) - (1)));
      for (;ROGUE_COND(((_auto_279_0) <= (_auto_280_0)));++_auto_279_0)
      {
        ROGUE_GC_CHECK;
        ROGUE_DEF_LOCAL_REF(RogueClassValue*,library_0,(((RogueClassValue*)Rogue_call_ROGUEM8( 32, _auto_278_0, _auto_279_0 ))));
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
  RogueSystem__exit__Int32( ROGUE_ARG((RogueSystem__run__String( ROGUE_ARG(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], exe_4 ))) )))), Rogue_literal_strings[60] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], ROGUE_ARG(((RogueString*)(RogueString_List__join__String( ROGUE_ARG(((RogueString_List*)(RogueString_List__mapped_String____Function_String_RETURNSString_( ROGUE_ARG(RogueSystem_command_line_arguments), ROGUE_ARG(((RogueClass_Function_String_RETURNSString_*)(((RogueClassFunction_281*)ROGUE_SINGLETON(Function_281))))) )))), Rogue_literal_strings[60] )))) ))) )))) )))) ))) );
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
      ROGUE_DEF_LOCAL_REF(Rogue_Function____List*,_auto_282_0,(functions_0));
      RogueInt32 _auto_283_0 = (0);
      RogueInt32 _auto_284_0 = (((_auto_282_0->count) - (1)));
      for (;ROGUE_COND(((_auto_283_0) <= (_auto_284_0)));++_auto_283_0)
      {
        ROGUE_GC_CHECK;
        ROGUE_DEF_LOCAL_REF(RogueClass_Function___*,fn_0,(((RogueClass_Function___*)(_auto_282_0->data->as_objects[_auto_283_0]))));
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
    case 58:
      return (RogueClassPrintWriter*)RogueConsole__close( (RogueClassConsole*)THIS );
    case 60:
      return (RogueClassPrintWriter*)RogueConsoleErrorPrinter__close( (RogueClassConsoleErrorPrinter*)THIS );
    case 79:
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
    case 58:
      return (RogueClassPrintWriter*)RogueConsole__flush( (RogueClassConsole*)THIS );
    case 60:
      return (RogueClassPrintWriter*)RogueConsoleErrorPrinter__flush( (RogueClassConsoleErrorPrinter*)THIS );
    case 79:
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
    case 58:
      return (RogueClassPrintWriter*)RogueConsole__println__String( (RogueClassConsole*)THIS, value_0 );
    case 60:
      return (RogueClassPrintWriter*)RogueConsoleErrorPrinter__println__String( (RogueClassConsoleErrorPrinter*)THIS, value_0 );
    case 79:
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
    case 58:
      return (RogueClassPrintWriter*)RogueConsole__write__StringBuilder( (RogueClassConsole*)THIS, buffer_0 );
    case 60:
      return (RogueClassPrintWriter*)RogueConsoleErrorPrinter__write__StringBuilder( (RogueClassConsoleErrorPrinter*)THIS, buffer_0 );
    case 79:
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
  return (RogueString*)(Rogue_literal_strings[178]);
}

RogueLogical RogueValueTable__contains__String( RogueClassValueTable* THIS, RogueString* key_0 )
{
  ROGUE_GC_CHECK;
  return (RogueLogical)(((RogueTable_String_Value___contains__String( ROGUE_ARG(THIS->data), key_0 ))));
}

RogueLogical RogueValueTable__contains__Value( RogueClassValueTable* THIS, RogueClassValue* key_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((!((RogueOptionalValue__operator__Value( key_0 )))) || (!((Rogue_call_ROGUEM9( 49, key_0 )))))))
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
  ROGUE_DEF_LOCAL_REF(RogueClassValueTable*,_auto_355_0,(((RogueClassValueTable*)(RogueValueTable__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassValueTable*,ROGUE_CREATE_OBJECT(ValueTable))) )))));
  {
  }
  table_1 = ((RogueClassValueTable*)(_auto_355_0));
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
    ROGUE_DEF_LOCAL_REF(RogueString_List*,_auto_375_0,(((RogueString_List*)(RogueTable_String_Value___keys__String_List( ROGUE_ARG(THIS->data), ROGUE_ARG(((RogueString_List*)(NULL))) )))));
    RogueInt32 _auto_376_0 = (0);
    RogueInt32 _auto_377_0 = (((_auto_375_0->count) - (1)));
    for (;ROGUE_COND(((_auto_376_0) <= (_auto_377_0)));++_auto_376_0)
    {
      ROGUE_GC_CHECK;
      ROGUE_DEF_LOCAL_REF(RogueString*,key_0,(((RogueString*)(_auto_375_0->data->as_objects[_auto_376_0]))));
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
        indent_4 = ((RogueLogical)(((((void*)value_5) == ((void*)NULL)) || (!((Rogue_call_ROGUEM9( 38, value_5 )))))));
        if (ROGUE_COND(indent_4))
        {
          buffer_0->indent += 2;
        }
      }
      if (ROGUE_COND(((((void*)value_5) != ((void*)NULL)) && ((((RogueOptionalValue__operator__Value( value_5 ))) || ((Rogue_call_ROGUEM9( 43, value_5 ))))))))
      {
        Rogue_call_ROGUEM17( 115, value_5, buffer_0, flags_1 );
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
  return (RogueString*)(Rogue_literal_strings[139]);
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
  ROGUE_DEF_LOCAL_REF(RogueClassValueTable*,_auto_338_0,(((RogueClassValueTable*)(RogueValueTable__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassValueTable*,ROGUE_CREATE_OBJECT(ValueTable))) )))));
  {
  }
  return (RogueClassValueTable*)(_auto_338_0);
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
  if (ROGUE_COND(!((Rogue_call_ROGUEM9( 38, ROGUE_ARG(THIS) )))))
  {
    return (RogueLogical)(false);
  }
  if (ROGUE_COND((((Rogue_call_ROGUEM7( 22, ROGUE_ARG(THIS) ))) > (1))))
  {
    return (RogueLogical)(true);
  }
  {
    ROGUE_DEF_LOCAL_REF(RogueClassValue*,_auto_341_0,(THIS));
    RogueInt32 _auto_342_0 = (0);
    RogueInt32 _auto_343_0 = ((((Rogue_call_ROGUEM7( 22, _auto_341_0 ))) - (1)));
    for (;ROGUE_COND(((_auto_342_0) <= (_auto_343_0)));++_auto_342_0)
    {
      ROGUE_GC_CHECK;
      ROGUE_DEF_LOCAL_REF(RogueClassValue*,value_0,(((RogueClassValue*)Rogue_call_ROGUEM8( 32, _auto_341_0, _auto_342_0 ))));
      if (ROGUE_COND((((RogueOptionalValue__operator__Value( value_0 ))) && (((RogueValue__is_complex( value_0 )))))))
      {
        return (RogueLogical)(true);
      }
    }
  }
  return (RogueLogical)(false);
}

RogueLogical RogueValue__is_int32( RogueClassValue* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueLogical)(false);
}

RogueLogical RogueValue__is_int64( RogueClassValue* THIS )
{
  ROGUE_GC_CHECK;
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

RogueLogical RogueValue__is_object( RogueClassValue* THIS )
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
  return (RogueLogical)((Rogue_call_ROGUEM6( 59, ROGUE_ARG(THIS), ROGUE_ARG((RogueValue__create__String( other_0 ))) )));
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

RogueByte RogueValue__to_Byte( RogueClassValue* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueByte)(((RogueByte)((Rogue_call_ROGUEM7( 107, ROGUE_ARG(THIS) )))));
}

RogueCharacter RogueValue__to_Character( RogueClassValue* THIS )
{
  ROGUE_GC_CHECK;
  ROGUE_DEF_LOCAL_REF(RogueString*,st_0,(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)THIS)) ))));
  if (ROGUE_COND(!!(st_0->character_count)))
  {
    return (RogueCharacter)(RogueString_character_at(st_0,0));
  }
  return (RogueCharacter)((RogueCharacter__create__Int32( 0 )));
}

RogueInt64 RogueValue__to_Int64( RogueClassValue* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueInt64)(0LL);
}

RogueInt32 RogueValue__to_Int32( RogueClassValue* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueInt32)(((RogueInt32)((Rogue_call_ROGUEM2( 106, ROGUE_ARG(THIS) )))));
}

RogueLogical RogueValue__to_Logical( RogueClassValue* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueLogical)((RogueLogical__create__Int32( ROGUE_ARG((Rogue_call_ROGUEM7( 107, ROGUE_ARG(THIS) ))) )));
}

RogueReal64 RogueValue__to_Real64( RogueClassValue* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueReal64)(((RogueReal64)((Rogue_call_ROGUEM7( 107, ROGUE_ARG(THIS) )))));
}

RogueReal32 RogueValue__to_Real32( RogueClassValue* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueReal32)(((RogueReal32)((Rogue_call_ROGUEM13( 109, ROGUE_ARG(THIS) )))));
}

RogueObject* RogueValue__to_Object( RogueClassValue* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueObject*)(((RogueObject*)(NULL)));
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
  return (RogueStringBuilder*)(((RogueStringBuilder*)Rogue_call_ROGUEM17( 115, ROGUE_ARG(THIS), buffer_0, flags_3 )));
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
  return (RogueString*)(Rogue_literal_strings[157]);
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
    if (ROGUE_COND((Rogue_call_ROGUEM42( 13, ROGUE_ARG(THIS->sort_function), entry_0, ROGUE_ARG(entry_0->next_entry) ))))
    {
      return;
    }
  }
  else if (ROGUE_COND(((void*)entry_0) == ((void*)THIS->last_entry)))
  {
    if (ROGUE_COND((Rogue_call_ROGUEM42( 13, ROGUE_ARG(THIS->sort_function), ROGUE_ARG(entry_0->previous_entry), entry_0 ))))
    {
      return;
    }
  }
  else if (ROGUE_COND((((Rogue_call_ROGUEM42( 13, ROGUE_ARG(THIS->sort_function), ROGUE_ARG(entry_0->previous_entry), entry_0 ))) && ((Rogue_call_ROGUEM42( 13, ROGUE_ARG(THIS->sort_function), entry_0, ROGUE_ARG(entry_0->next_entry) ))))))
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
      if (ROGUE_COND((Rogue_call_ROGUEM42( 13, ROGUE_ARG(THIS->sort_function), entry_0, ROGUE_ARG(THIS->first_entry) ))))
      {
        entry_0->next_entry = THIS->first_entry;
        THIS->first_entry->previous_entry = entry_0;
        THIS->first_entry = entry_0;
      }
      else if (ROGUE_COND((Rogue_call_ROGUEM42( 13, ROGUE_ARG(THIS->sort_function), ROGUE_ARG(THIS->last_entry), entry_0 ))))
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
          if (ROGUE_COND((Rogue_call_ROGUEM42( 13, ROGUE_ARG(THIS->sort_function), entry_0, ROGUE_ARG(cur_1->next_entry) ))))
          {
            entry_0->previous_entry = cur_1;
            entry_0->next_entry = cur_1->next_entry;
            entry_0->next_entry->previous_entry = entry_0;
            cur_1->next_entry = entry_0;
            goto _auto_404;
          }
          cur_1 = ((RogueClassTableEntry_String_Value_*)(cur_1->next_entry));
        }
        _auto_404:;
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

RogueInt32 RogueInt32__hash_code( RogueInt32 THIS )
{
  ROGUE_GC_CHECK;
  return (RogueInt32)(THIS);
}

RogueInt32 RogueInt32__or_smaller__Int32( RogueInt32 THIS, RogueInt32 other_0 )
{
  ROGUE_GC_CHECK;
  return (RogueInt32)(((((((THIS) <= (other_0))))) ? (THIS) : other_0));
}

RogueString* RogueInt32__to_String( RogueInt32 THIS )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(((RogueString*)(RogueString__operatorPLUS__Int32( ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], Rogue_literal_strings[0] ))), ROGUE_ARG(THIS) ))));
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
  return (RogueString*)(Rogue_literal_strings[199]);
}

RogueString* RogueArray__type_name( RogueArray* THIS )
{
  return (RogueString*)(Rogue_literal_strings[144]);
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
  return (RogueString*)(Rogue_literal_strings[164]);
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
  return (RogueString*)(Rogue_literal_strings[140]);
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

RogueString* RogueString__operatorPLUS__Real64( RogueString* THIS, RogueReal64 value_0 )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__Real64( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), ROGUE_ARG(THIS) )))), value_0 )))) ))));
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
  return (RogueString*)(Rogue_literal_strings[165]);
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
  return (RogueString*)(Rogue_literal_strings[142]);
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
    ROGUE_DEF_LOCAL_REF(RogueStringBuilder*,_auto_301_0,(THIS));
    RogueInt32 i_0 = (0);
    RogueInt32 _auto_302_0 = (((_auto_301_0->count) - (1)));
    for (;ROGUE_COND(((i_0) <= (_auto_302_0)));++i_0)
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
    RogueStringBuilder__print__String( ROGUE_ARG(THIS), Rogue_literal_strings[134] );
    return (RogueStringBuilder*)(THIS);
  }
  else if (ROGUE_COND(((RogueReal64__is_infinite( value_0 )))))
  {
    if (ROGUE_COND(((value_0) < (0.0))))
    {
      RogueStringBuilder__print__String( ROGUE_ARG(THIS), Rogue_literal_strings[135] );
    }
    else
    {
      RogueStringBuilder__print__String( ROGUE_ARG(THIS), Rogue_literal_strings[136] );
    }
    return (RogueStringBuilder*)(THIS);
  }
  else if (ROGUE_COND(((RogueReal64__is_not_a_number( value_0 )))))
  {
    RogueStringBuilder__print__String( ROGUE_ARG(THIS), Rogue_literal_strings[137] );
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
        goto _auto_308;
      }
    }
  }
  _auto_308:;
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
      RogueStringBuilder__print__String( ROGUE_ARG(THIS), Rogue_literal_strings[135] );
    }
    else
    {
      RogueStringBuilder__print__String( ROGUE_ARG(THIS), Rogue_literal_strings[136] );
    }
    return (RogueStringBuilder*)(THIS);
  }
  else if (ROGUE_COND(((RogueReal64__is_not_a_number( value_0 )))))
  {
    RogueStringBuilder__print__String( ROGUE_ARG(THIS), Rogue_literal_strings[137] );
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
        ROGUE_DEF_LOCAL_REF(RogueString*,_auto_309_0,(value_0));
        RogueInt32 _auto_310_0 = (0);
        RogueInt32 _auto_311_0 = (((_auto_309_0->character_count) - (1)));
        for (;ROGUE_COND(((_auto_310_0) <= (_auto_311_0)));++_auto_310_0)
        {
          ROGUE_GC_CHECK;
          RogueCharacter ch_0 = (RogueString_character_at(_auto_309_0,_auto_310_0));
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
    ROGUE_DEF_LOCAL_REF(RogueByte_List*,_auto_312_0,(RogueStringBuilder_work_bytes));
    RogueInt32 _auto_313_0 = (0);
    RogueInt32 _auto_314_0 = (((_auto_312_0->count) - (1)));
    for (;ROGUE_COND(((_auto_313_0) <= (_auto_314_0)));++_auto_313_0)
    {
      ROGUE_GC_CHECK;
      RogueByte digit_0 = (_auto_312_0->data->as_bytes[_auto_313_0]);
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
    ROGUE_DEF_LOCAL_REF(RogueByte_List*,_auto_315_0,(RogueStringBuilder_work_bytes));
    RogueInt32 _auto_316_0 = (0);
    RogueInt32 _auto_317_0 = (((_auto_315_0->count) - (1)));
    for (;ROGUE_COND(((_auto_316_0) <= (_auto_317_0)));++_auto_316_0)
    {
      ROGUE_GC_CHECK;
      RogueByte digit_0 = (_auto_315_0->data->as_bytes[_auto_316_0]);
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
    ROGUE_DEF_LOCAL_REF(RogueByte_List*,_auto_407_0,(THIS));
    RogueInt32 _auto_408_0 = (0);
    RogueInt32 _auto_409_0 = (((_auto_407_0->count) - (1)));
    for (;ROGUE_COND(((_auto_408_0) <= (_auto_409_0)));++_auto_408_0)
    {
      ROGUE_GC_CHECK;
      RogueByte value_0 = (_auto_407_0->data->as_bytes[_auto_408_0]);
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
  return (RogueString*)(Rogue_literal_strings[208]);
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
  return (RogueString*)(Rogue_literal_strings[143]);
}

RogueString* RogueByte__to_String( RogueByte THIS )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(((RogueString*)(RogueString__operatorPLUS__Int32( ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], Rogue_literal_strings[0] ))) ))), ROGUE_ARG(((RogueInt32)(THIS))) ))));
}

RogueString* RogueArray_Byte___type_name( RogueArray* THIS )
{
  return (RogueString*)(Rogue_literal_strings[198]);
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
  return (RogueString*)(Rogue_literal_strings[162]);
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
    ROGUE_DEF_LOCAL_REF(RogueStringBuilder_List*,_auto_480_0,(THIS));
    RogueInt32 _auto_481_0 = (0);
    RogueInt32 _auto_482_0 = (((_auto_480_0->count) - (1)));
    for (;ROGUE_COND(((_auto_481_0) <= (_auto_482_0)));++_auto_481_0)
    {
      ROGUE_GC_CHECK;
      ROGUE_DEF_LOCAL_REF(RogueStringBuilder*,value_0,(((RogueStringBuilder*)(_auto_480_0->data->as_objects[_auto_481_0]))));
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
  return (RogueString*)(Rogue_literal_strings[214]);
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
  return (RogueString*)(Rogue_literal_strings[200]);
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
    ROGUE_DEF_LOCAL_REF(Rogue_Function____List*,_auto_556_0,(THIS));
    RogueInt32 _auto_557_0 = (0);
    RogueInt32 _auto_558_0 = (((_auto_556_0->count) - (1)));
    for (;ROGUE_COND(((_auto_557_0) <= (_auto_558_0)));++_auto_557_0)
    {
      ROGUE_GC_CHECK;
      ROGUE_DEF_LOCAL_REF(RogueClass_Function___*,value_0,(((RogueClass_Function___*)(_auto_556_0->data->as_objects[_auto_557_0]))));
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
  return (RogueString*)(Rogue_literal_strings[213]);
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
  return (RogueString*)(Rogue_literal_strings[150]);
}

void Rogue_Function_____call( RogueClass_Function___* THIS )
{
}

RogueString* RogueArray__Function______type_name( RogueArray* THIS )
{
  return (RogueString*)(Rogue_literal_strings[197]);
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
  return (RogueString*)(Rogue_literal_strings[147]);
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
    ROGUE_DEF_LOCAL_REF(RogueString_List*,_auto_285_0,(THIS->entries));
    RogueInt32 _auto_286_0 = (0);
    RogueInt32 _auto_287_0 = (((_auto_285_0->count) - (1)));
    for (;ROGUE_COND(((_auto_286_0) <= (_auto_287_0)));++_auto_286_0)
    {
      ROGUE_GC_CHECK;
      ROGUE_DEF_LOCAL_REF(RogueString*,entry_0,(((RogueString*)(_auto_285_0->data->as_objects[_auto_286_0]))));
      RogueOptionalInt32 sp_1 = (((RogueString__locate__Character_OptionalInt32( entry_0, (RogueCharacter)' ', (RogueOptionalInt32__create()) ))));
      if (ROGUE_COND(sp_1.exists))
      {
        max_characters_0 = ((RogueInt32)((RogueMath__max__Int32_Int32( max_characters_0, ROGUE_ARG(sp_1.value) ))));
      }
    }
  }
  ++max_characters_0;
  {
    ROGUE_DEF_LOCAL_REF(RogueString_List*,_auto_288_0,(THIS->entries));
    RogueInt32 i_0 = (0);
    RogueInt32 _auto_289_0 = (((_auto_288_0->count) - (1)));
    for (;ROGUE_COND(((i_0) <= (_auto_289_0)));++i_0)
    {
      ROGUE_GC_CHECK;
      ROGUE_DEF_LOCAL_REF(RogueString*,entry_0,(((RogueString*)(_auto_288_0->data->as_objects[i_0]))));
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
    ROGUE_DEF_LOCAL_REF(RogueString_List*,_auto_290_0,(THIS->entries));
    RogueInt32 _auto_291_0 = (0);
    RogueInt32 _auto_292_0 = (((_auto_290_0->count) - (1)));
    for (;ROGUE_COND(((_auto_291_0) <= (_auto_292_0)));++_auto_291_0)
    {
      ROGUE_GC_CHECK;
      ROGUE_DEF_LOCAL_REF(RogueString*,entry_0,(((RogueString*)(_auto_290_0->data->as_objects[_auto_291_0]))));
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
    ROGUE_DEF_LOCAL_REF(RogueString_List*,_auto_632_0,(THIS));
    RogueInt32 _auto_633_0 = (0);
    RogueInt32 _auto_634_0 = (((_auto_632_0->count) - (1)));
    for (;ROGUE_COND(((_auto_633_0) <= (_auto_634_0)));++_auto_633_0)
    {
      ROGUE_GC_CHECK;
      ROGUE_DEF_LOCAL_REF(RogueString*,value_0,(((RogueString*)(_auto_632_0->data->as_objects[_auto_633_0]))));
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
  return (RogueString*)(Rogue_literal_strings[212]);
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
        ROGUE_DEF_LOCAL_REF(RogueString_List*,_auto_671_0,(THIS));
        RogueInt32 i_0 = (0);
        RogueInt32 _auto_672_0 = (((_auto_671_0->count) - (1)));
        for (;ROGUE_COND(((i_0) <= (_auto_672_0)));++i_0)
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
    ROGUE_DEF_LOCAL_REF(RogueString_List*,_auto_673_0,(THIS));
    RogueInt32 i_0 = (0);
    RogueInt32 _auto_674_0 = (((_auto_673_0->count) - (1)));
    for (;ROGUE_COND(((i_0) <= (_auto_674_0)));++i_0)
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
    ROGUE_DEF_LOCAL_REF(RogueString_List*,_auto_695_0,(THIS));
    RogueInt32 _auto_696_0 = (0);
    RogueInt32 _auto_697_0 = (((_auto_695_0->count) - (1)));
    for (;ROGUE_COND(((_auto_696_0) <= (_auto_697_0)));++_auto_696_0)
    {
      ROGUE_GC_CHECK;
      ROGUE_DEF_LOCAL_REF(RogueString*,line_0,(((RogueString*)(_auto_695_0->data->as_objects[_auto_696_0]))));
      total_count_1 += line_0->character_count;
    }
  }
  ROGUE_DEF_LOCAL_REF(RogueStringBuilder*,builder_2,(((RogueStringBuilder*)(RogueStringBuilder__init__Int32( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))), total_count_1 )))));
  RogueLogical first_3 = (true);
  {
    ROGUE_DEF_LOCAL_REF(RogueString_List*,_auto_698_0,(THIS));
    RogueInt32 _auto_699_0 = (0);
    RogueInt32 _auto_700_0 = (((_auto_698_0->count) - (1)));
    for (;ROGUE_COND(((_auto_699_0) <= (_auto_700_0)));++_auto_699_0)
    {
      ROGUE_GC_CHECK;
      ROGUE_DEF_LOCAL_REF(RogueString*,line_0,(((RogueString*)(_auto_698_0->data->as_objects[_auto_699_0]))));
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
    ROGUE_DEF_LOCAL_REF(RogueString_List*,_auto_702_0,(THIS));
    RogueInt32 _auto_703_0 = (0);
    RogueInt32 _auto_704_0 = (((_auto_702_0->count) - (1)));
    for (;ROGUE_COND(((_auto_703_0) <= (_auto_704_0)));++_auto_703_0)
    {
      ROGUE_GC_CHECK;
      ROGUE_DEF_LOCAL_REF(RogueString*,element_0,(((RogueString*)(_auto_702_0->data->as_objects[_auto_703_0]))));
      RogueString_List__add__String( result_1, ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM3( 13, map_fn_0, element_0 ))) );
    }
  }
  return (RogueString_List*)(result_1);
}

RogueString* RogueArray_String___type_name( RogueArray* THIS )
{
  return (RogueString*)(Rogue_literal_strings[196]);
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

RogueString* RogueReal64__to_String( RogueReal64 THIS )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(((RogueString*)(RogueString__operatorPLUS__Real64( ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], Rogue_literal_strings[0] ))), ROGUE_ARG(THIS) ))));
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

RogueString* RogueReal32__to_String( RogueReal32 THIS )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(((RogueString*)(RogueString__operatorPLUS__Real64( ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], Rogue_literal_strings[0] ))) ))), ROGUE_ARG(((RogueReal64)(THIS))) ))));
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

RogueString* RogueInt64__to_String( RogueInt64 THIS )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__Int64( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), ROGUE_ARG(THIS) )))) ))));
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

RogueTypeInfo* RogueTypeInfo__init_object( RogueTypeInfo* THIS )
{
  ROGUE_GC_CHECK;
  RogueObject__init_object( ROGUE_ARG(((RogueObject*)THIS)) );
  THIS->global_properties = ((RoguePropertyInfo_List*)(RoguePropertyInfo_List__init( ROGUE_ARG(ROGUE_CREATE_REF(RoguePropertyInfo_List*,ROGUE_CREATE_OBJECT(PropertyInfo_List))) )));
  THIS->properties = ((RoguePropertyInfo_List*)(RoguePropertyInfo_List__init( ROGUE_ARG(ROGUE_CREATE_REF(RoguePropertyInfo_List*,ROGUE_CREATE_OBJECT(PropertyInfo_List))) )));
  THIS->methods = ((RogueMethodInfo_List*)(RogueMethodInfo_List__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueMethodInfo_List*,ROGUE_CREATE_OBJECT(MethodInfo_List))) )));
  return (RogueTypeInfo*)(THIS);
}

RogueString* RogueTypeInfo__to_String( RogueTypeInfo* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(THIS->name);
}

RogueString* RogueTypeInfo__type_name( RogueTypeInfo* THIS )
{
  return (RogueString*)(Rogue_literal_strings[141]);
}

RogueTypeInfo* RogueTypeInfo__init__Int32_String( RogueTypeInfo* THIS, RogueInt32 _auto_98_0, RogueString* _auto_99_1 )
{
  ROGUE_GC_CHECK;
  THIS->name = _auto_99_1;
  THIS->index = _auto_98_0;
  return (RogueTypeInfo*)(THIS);
}

void RogueTypeInfo__add_global_property_info__Int32_Int32( RogueTypeInfo* THIS, RogueInt32 global_property_name_index_0, RogueInt32 global_property_type_index_1 )
{
  ROGUE_GC_CHECK;
  RoguePropertyInfo_List__add__PropertyInfo( ROGUE_ARG(THIS->global_properties), ROGUE_ARG(((RogueClassPropertyInfo*)(RoguePropertyInfo__init__Int32_Int32_Int32( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassPropertyInfo*,ROGUE_CREATE_OBJECT(PropertyInfo))), ROGUE_ARG(THIS->global_properties->count), global_property_name_index_0, global_property_type_index_1 )))) );
}

void RogueTypeInfo__add_property_info__Int32_Int32( RogueTypeInfo* THIS, RogueInt32 property_name_index_0, RogueInt32 property_type_index_1 )
{
  ROGUE_GC_CHECK;
  RoguePropertyInfo_List__add__PropertyInfo( ROGUE_ARG(THIS->properties), ROGUE_ARG(((RogueClassPropertyInfo*)(RoguePropertyInfo__init__Int32_Int32_Int32( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassPropertyInfo*,ROGUE_CREATE_OBJECT(PropertyInfo))), ROGUE_ARG(THIS->properties->count), property_name_index_0, property_type_index_1 )))) );
}

void RogueTypeInfo__add_method_info__Int32( RogueTypeInfo* THIS, RogueInt32 method_index_0 )
{
  ROGUE_GC_CHECK;
  RogueInt32 first_param_name_1 = 0;
  RogueInt32 first_param_type_2 = 0;
  RogueInt32 parameter_count_3 = 0;
  RogueInt32 return_type_4 = 0;
  RogueInt32 call_handler_5 = 0;
  RogueLogical success_6 = (false);
  if (Rogue_method_info_table[method_index_0][0] >= 0)
  {
    success_6 = 1;
    parameter_count_3 = Rogue_method_info_table[method_index_0][1];
    first_param_name_1 = Rogue_method_info_table[method_index_0][2];
    first_param_type_2 = Rogue_method_info_table[method_index_0][3];
    return_type_4      = Rogue_method_info_table[method_index_0][4];
    call_handler_5     = Rogue_method_info_table[method_index_0][5];
  }

  if (ROGUE_COND(success_6))
  {
    RogueMethodInfo_List__add__MethodInfo( ROGUE_ARG(THIS->methods), ROGUE_ARG(((RogueClassMethodInfo*)(RogueMethodInfo__init__Int32_Int32_Int32_Int32_Int32_Int32( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassMethodInfo*,ROGUE_CREATE_OBJECT(MethodInfo))), method_index_0, parameter_count_3, first_param_name_1, first_param_type_2, return_type_4, call_handler_5 )))) );
  }
}

RoguePropertyInfo_List* RoguePropertyInfo_List__init_object( RoguePropertyInfo_List* THIS )
{
  ROGUE_GC_CHECK;
  RogueGenericList__init_object( ROGUE_ARG(((RogueClassGenericList*)THIS)) );
  return (RoguePropertyInfo_List*)(THIS);
}

RoguePropertyInfo_List* RoguePropertyInfo_List__init( RoguePropertyInfo_List* THIS )
{
  ROGUE_GC_CHECK;
  RoguePropertyInfo_List__init__Int32( ROGUE_ARG(THIS), 0 );
  return (RoguePropertyInfo_List*)(THIS);
}

RogueString* RoguePropertyInfo_List__to_String( RoguePropertyInfo_List* THIS )
{
  ROGUE_GC_CHECK;
  ROGUE_DEF_LOCAL_REF(RogueStringBuilder*,buffer_0,(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))));
  RogueStringBuilder__print__Character_Logical( buffer_0, (RogueCharacter)'[', true );
  RogueLogical first_1 = (true);
  {
    ROGUE_DEF_LOCAL_REF(RoguePropertyInfo_List*,_auto_743_0,(THIS));
    RogueInt32 _auto_744_0 = (0);
    RogueInt32 _auto_745_0 = (((_auto_743_0->count) - (1)));
    for (;ROGUE_COND(((_auto_744_0) <= (_auto_745_0)));++_auto_744_0)
    {
      ROGUE_GC_CHECK;
      ROGUE_DEF_LOCAL_REF(RogueClassPropertyInfo*,value_0,(((RogueClassPropertyInfo*)(_auto_743_0->data->as_objects[_auto_744_0]))));
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
        RogueStringBuilder__print__String( buffer_0, ROGUE_ARG(((RogueString*)(RoguePropertyInfo__to_String( value_0 )))) );
      }
    }
  }
  RogueStringBuilder__print__Character_Logical( buffer_0, (RogueCharacter)']', true );
  return (RogueString*)(((RogueString*)(RogueStringBuilder__to_String( buffer_0 ))));
}

RogueString* RoguePropertyInfo_List__type_name( RoguePropertyInfo_List* THIS )
{
  return (RogueString*)(Rogue_literal_strings[211]);
}

RoguePropertyInfo_List* RoguePropertyInfo_List__init__Int32( RoguePropertyInfo_List* THIS, RogueInt32 initial_capacity_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(!!(initial_capacity_0)))
  {
    THIS->data = RogueType_create_array( initial_capacity_0, sizeof(RogueClassPropertyInfo*), true, 35 );
  }
  return (RoguePropertyInfo_List*)(THIS);
}

RoguePropertyInfo_List* RoguePropertyInfo_List__add__PropertyInfo( RoguePropertyInfo_List* THIS, RogueClassPropertyInfo* value_0 )
{
  ROGUE_GC_CHECK;
  ((RoguePropertyInfo_List*)(RoguePropertyInfo_List__reserve__Int32( ROGUE_ARG(THIS), 1 )))->data->as_objects[THIS->count] = value_0;
  THIS->count = ((THIS->count) + (1));
  return (RoguePropertyInfo_List*)(THIS);
}

RogueInt32 RoguePropertyInfo_List__capacity( RoguePropertyInfo_List* THIS )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(!(!!(THIS->data))))
  {
    return (RogueInt32)(0);
  }
  return (RogueInt32)(THIS->data->count);
}

RoguePropertyInfo_List* RoguePropertyInfo_List__reserve__Int32( RoguePropertyInfo_List* THIS, RogueInt32 additional_elements_0 )
{
  ROGUE_GC_CHECK;
  RogueInt32 required_capacity_1 = (((THIS->count) + (additional_elements_0)));
  if (ROGUE_COND(!(!!(THIS->data))))
  {
    if (ROGUE_COND(((required_capacity_1) < (10))))
    {
      required_capacity_1 = ((RogueInt32)(10));
    }
    THIS->data = RogueType_create_array( required_capacity_1, sizeof(RogueClassPropertyInfo*), true, 35 );
  }
  else if (ROGUE_COND(((required_capacity_1) > (THIS->data->count))))
  {
    RogueInt32 cap_2 = (((RoguePropertyInfo_List__capacity( ROGUE_ARG(THIS) ))));
    if (ROGUE_COND(((required_capacity_1) < (((cap_2) + (cap_2))))))
    {
      required_capacity_1 = ((RogueInt32)(((cap_2) + (cap_2))));
    }
    ROGUE_DEF_LOCAL_REF(RogueArray*,new_data_3,(RogueType_create_array( required_capacity_1, sizeof(RogueClassPropertyInfo*), true, 35 )));
    RogueArray_set(new_data_3,0,((RogueArray*)(THIS->data)),0,-1);
    THIS->data = new_data_3;
  }
  return (RoguePropertyInfo_List*)(THIS);
}

RogueClassPropertyInfo* RoguePropertyInfo__init_object( RogueClassPropertyInfo* THIS )
{
  ROGUE_GC_CHECK;
  RogueObject__init_object( ROGUE_ARG(((RogueObject*)THIS)) );
  return (RogueClassPropertyInfo*)(THIS);
}

RogueString* RoguePropertyInfo__to_String( RogueClassPropertyInfo* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], ROGUE_ARG(((RogueString*)(RoguePropertyInfo__name( ROGUE_ARG(THIS) )))) ))) )))), Rogue_literal_strings[76] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], ROGUE_ARG(((RogueTypeInfo*)(Rogue_PropertyInfo__type( ROGUE_ARG(THIS) )))->name) ))) )))) ))));
}

RogueString* RoguePropertyInfo__type_name( RogueClassPropertyInfo* THIS )
{
  return (RogueString*)(Rogue_literal_strings[145]);
}

RogueClassPropertyInfo* RoguePropertyInfo__init__Int32_Int32_Int32( RogueClassPropertyInfo* THIS, RogueInt32 _auto_126_0, RogueInt32 _auto_127_1, RogueInt32 _auto_128_2 )
{
  ROGUE_GC_CHECK;
  THIS->property_type_index = _auto_128_2;
  THIS->property_name_index = _auto_127_1;
  THIS->index = _auto_126_0;
  return (RogueClassPropertyInfo*)(THIS);
}

RogueString* RoguePropertyInfo__name( RogueClassPropertyInfo* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueString*)((RogueRuntime__literal_string__Int32( ROGUE_ARG(THIS->property_name_index) )));
}

RogueTypeInfo* Rogue_PropertyInfo__type( RogueClassPropertyInfo* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueTypeInfo*)((RogueTypeInfo__get__Int32( ROGUE_ARG(THIS->property_type_index) )));
}

RogueString* RogueArray_PropertyInfo___type_name( RogueArray* THIS )
{
  return (RogueString*)(Rogue_literal_strings[195]);
}

RogueMethodInfo_List* RogueMethodInfo_List__init_object( RogueMethodInfo_List* THIS )
{
  ROGUE_GC_CHECK;
  RogueGenericList__init_object( ROGUE_ARG(((RogueClassGenericList*)THIS)) );
  return (RogueMethodInfo_List*)(THIS);
}

RogueMethodInfo_List* RogueMethodInfo_List__init( RogueMethodInfo_List* THIS )
{
  ROGUE_GC_CHECK;
  RogueMethodInfo_List__init__Int32( ROGUE_ARG(THIS), 0 );
  return (RogueMethodInfo_List*)(THIS);
}

RogueString* RogueMethodInfo_List__to_String( RogueMethodInfo_List* THIS )
{
  ROGUE_GC_CHECK;
  ROGUE_DEF_LOCAL_REF(RogueStringBuilder*,buffer_0,(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))));
  RogueStringBuilder__print__Character_Logical( buffer_0, (RogueCharacter)'[', true );
  RogueLogical first_1 = (true);
  {
    ROGUE_DEF_LOCAL_REF(RogueMethodInfo_List*,_auto_819_0,(THIS));
    RogueInt32 _auto_820_0 = (0);
    RogueInt32 _auto_821_0 = (((_auto_819_0->count) - (1)));
    for (;ROGUE_COND(((_auto_820_0) <= (_auto_821_0)));++_auto_820_0)
    {
      ROGUE_GC_CHECK;
      ROGUE_DEF_LOCAL_REF(RogueClassMethodInfo*,value_0,(((RogueClassMethodInfo*)(_auto_819_0->data->as_objects[_auto_820_0]))));
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
        RogueStringBuilder__print__String( buffer_0, ROGUE_ARG(((RogueString*)(RogueMethodInfo__to_String( value_0 )))) );
      }
    }
  }
  RogueStringBuilder__print__Character_Logical( buffer_0, (RogueCharacter)']', true );
  return (RogueString*)(((RogueString*)(RogueStringBuilder__to_String( buffer_0 ))));
}

RogueString* RogueMethodInfo_List__type_name( RogueMethodInfo_List* THIS )
{
  return (RogueString*)(Rogue_literal_strings[215]);
}

RogueMethodInfo_List* RogueMethodInfo_List__init__Int32( RogueMethodInfo_List* THIS, RogueInt32 initial_capacity_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(!!(initial_capacity_0)))
  {
    THIS->data = RogueType_create_array( initial_capacity_0, sizeof(RogueClassMethodInfo*), true, 38 );
  }
  return (RogueMethodInfo_List*)(THIS);
}

RogueMethodInfo_List* RogueMethodInfo_List__add__MethodInfo( RogueMethodInfo_List* THIS, RogueClassMethodInfo* value_0 )
{
  ROGUE_GC_CHECK;
  ((RogueMethodInfo_List*)(RogueMethodInfo_List__reserve__Int32( ROGUE_ARG(THIS), 1 )))->data->as_objects[THIS->count] = value_0;
  THIS->count = ((THIS->count) + (1));
  return (RogueMethodInfo_List*)(THIS);
}

RogueInt32 RogueMethodInfo_List__capacity( RogueMethodInfo_List* THIS )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(!(!!(THIS->data))))
  {
    return (RogueInt32)(0);
  }
  return (RogueInt32)(THIS->data->count);
}

RogueMethodInfo_List* RogueMethodInfo_List__reserve__Int32( RogueMethodInfo_List* THIS, RogueInt32 additional_elements_0 )
{
  ROGUE_GC_CHECK;
  RogueInt32 required_capacity_1 = (((THIS->count) + (additional_elements_0)));
  if (ROGUE_COND(!(!!(THIS->data))))
  {
    if (ROGUE_COND(((required_capacity_1) < (10))))
    {
      required_capacity_1 = ((RogueInt32)(10));
    }
    THIS->data = RogueType_create_array( required_capacity_1, sizeof(RogueClassMethodInfo*), true, 38 );
  }
  else if (ROGUE_COND(((required_capacity_1) > (THIS->data->count))))
  {
    RogueInt32 cap_2 = (((RogueMethodInfo_List__capacity( ROGUE_ARG(THIS) ))));
    if (ROGUE_COND(((required_capacity_1) < (((cap_2) + (cap_2))))))
    {
      required_capacity_1 = ((RogueInt32)(((cap_2) + (cap_2))));
    }
    ROGUE_DEF_LOCAL_REF(RogueArray*,new_data_3,(RogueType_create_array( required_capacity_1, sizeof(RogueClassMethodInfo*), true, 38 )));
    RogueArray_set(new_data_3,0,((RogueArray*)(THIS->data)),0,-1);
    THIS->data = new_data_3;
  }
  return (RogueMethodInfo_List*)(THIS);
}

RogueClassMethodInfo* RogueMethodInfo__init_object( RogueClassMethodInfo* THIS )
{
  ROGUE_GC_CHECK;
  RogueObject__init_object( ROGUE_ARG(((RogueObject*)THIS)) );
  return (RogueClassMethodInfo*)(THIS);
}

RogueString* RogueMethodInfo__to_String( RogueClassMethodInfo* THIS )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(!!(((RogueTypeInfo*)(Rogue_MethodInfo__return_type( ROGUE_ARG(THIS) ))))))
  {
    return (RogueString*)((RogueString__operatorPLUS__String_String( ROGUE_ARG((RogueString__operatorPLUS__String_String( ROGUE_ARG(((RogueString*)(RogueMethodInfo__signature( ROGUE_ARG(THIS) )))), Rogue_literal_strings[133] ))), ROGUE_ARG(((RogueTypeInfo*)(Rogue_MethodInfo__return_type( ROGUE_ARG(THIS) )))->name) )));
  }
  return (RogueString*)(((RogueString*)(RogueMethodInfo__signature( ROGUE_ARG(THIS) ))));
}

RogueString* RogueMethodInfo__type_name( RogueClassMethodInfo* THIS )
{
  return (RogueString*)(Rogue_literal_strings[166]);
}

RogueClassMethodInfo* RogueMethodInfo__init__Int32_Int32_Int32_Int32_Int32_Int32( RogueClassMethodInfo* THIS, RogueInt32 _auto_318_0, RogueInt32 _auto_319_1, RogueInt32 _auto_320_2, RogueInt32 _auto_321_3, RogueInt32 _auto_322_4, RogueInt32 _auto_323_5 )
{
  ROGUE_GC_CHECK;
  THIS->call_handler = _auto_323_5;
  THIS->__return_type = _auto_322_4;
  THIS->__first_param_type = _auto_321_3;
  THIS->_first_param_name = _auto_320_2;
  THIS->parameter_count = _auto_319_1;
  THIS->index = _auto_318_0;
  THIS->fn_ptr = ((RogueInt64)((intptr_t)Rogue_dynamic_method_table[THIS->index]));
  return (RogueClassMethodInfo*)(THIS);
}

RogueString* RogueMethodInfo__name( RogueClassMethodInfo* THIS )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(!!(THIS->name)))
  {
    return (RogueString*)(THIS->name);
  }
  THIS->name = (RogueMethodInfo___get_method_name__Int32( ROGUE_ARG(THIS->index) ));
  return (RogueString*)(THIS->name);
}

RogueTypeInfo* Rogue_MethodInfo__return_type( RogueClassMethodInfo* THIS )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((THIS->__return_type) == (-1))))
  {
    return (RogueTypeInfo*)(((RogueTypeInfo*)(NULL)));
  }
  return (RogueTypeInfo*)((RogueTypeInfo__get__Int32( ROGUE_ARG(THIS->__return_type) )));
}

RogueString* RogueMethodInfo__parameter_name__Int32( RogueClassMethodInfo* THIS, RogueInt32 index_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((((index_0) < (0))) || (((index_0) >= (THIS->parameter_count))))))
  {
    return (RogueString*)(((RogueString*)(NULL)));
  }
  return (RogueString*)(((RogueString*)(Rogue_literal_strings[ Rogue_method_param_names[THIS->_first_param_name + index_0] ])));
}

RogueTypeInfo* RogueMethodInfo__parameter_type__Int32( RogueClassMethodInfo* THIS, RogueInt32 index_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((((index_0) < (0))) || (((index_0) >= (THIS->parameter_count))))))
  {
    return (RogueTypeInfo*)(((RogueTypeInfo*)(NULL)));
  }
  return (RogueTypeInfo*)((RogueTypeInfo__get__Int32( ROGUE_ARG(((RogueInt32)(Rogue_method_param_types[THIS->__first_param_type + index_0]))) )));
}

RogueString* RogueMethodInfo__signature( RogueClassMethodInfo* THIS )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(!!(THIS->signature)))
  {
    return (RogueString*)(THIS->signature);
  }
  ROGUE_DEF_LOCAL_REF(RogueStringBuilder*,buffer_0,(((RogueStringBuilder*)(RogueStringBuilder__print__Character_Logical( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), ROGUE_ARG(((RogueString*)(RogueMethodInfo__name( ROGUE_ARG(THIS) )))) )))), (RogueCharacter)'(', true )))));
  {
    RogueInt32 i_1 = (0);
    RogueInt32 _auto_95_2 = (THIS->parameter_count);
    for (;ROGUE_COND(((i_1) < (_auto_95_2)));++i_1)
    {
      ROGUE_GC_CHECK;
      if (ROGUE_COND(((i_1) > (0))))
      {
        RogueStringBuilder__print__Character_Logical( buffer_0, (RogueCharacter)',', true );
      }
      RogueStringBuilder__print__String( buffer_0, ROGUE_ARG(((RogueTypeInfo*)(RogueMethodInfo__parameter_type__Int32( ROGUE_ARG(THIS), i_1 )))->name) );
    }
  }
  RogueStringBuilder__print__Character_Logical( buffer_0, (RogueCharacter)')', true );
  THIS->signature = ((RogueString*)(RogueStringBuilder__to_String( buffer_0 )));
  return (RogueString*)(THIS->signature);
}

RogueClassTable_Int32_String_* RogueTable_Int32_String___init_object( RogueClassTable_Int32_String_* THIS )
{
  ROGUE_GC_CHECK;
  RogueObject__init_object( ROGUE_ARG(((RogueObject*)THIS)) );
  return (RogueClassTable_Int32_String_*)(THIS);
}

RogueClassTable_Int32_String_* RogueTable_Int32_String___init( RogueClassTable_Int32_String_* THIS )
{
  ROGUE_GC_CHECK;
  RogueTable_Int32_String___init__Int32( ROGUE_ARG(THIS), 16 );
  return (RogueClassTable_Int32_String_*)(THIS);
}

RogueString* RogueTable_Int32_String___to_String( RogueClassTable_Int32_String_* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueTable_Int32_String___print_to__StringBuilder( ROGUE_ARG(THIS), ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))) )))) ))));
}

RogueString* RogueTable_Int32_String___type_name( RogueClassTable_Int32_String_* THIS )
{
  return (RogueString*)(Rogue_literal_strings[167]);
}

RogueClassTable_Int32_String_* RogueTable_Int32_String___init__Int32( RogueClassTable_Int32_String_* THIS, RogueInt32 bin_count_0 )
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
  THIS->bins = RogueType_create_array( bin_count_0, sizeof(RogueClassTableEntry_Int32_String_*), true, 41 );
  return (RogueClassTable_Int32_String_*)(THIS);
}

RogueClassTableEntry_Int32_String_* RogueTable_Int32_String___find__Int32( RogueClassTable_Int32_String_* THIS, RogueInt32 key_0 )
{
  ROGUE_GC_CHECK;
  RogueInt32 hash_1 = (((RogueInt32__hash_code( key_0 ))));
  ROGUE_DEF_LOCAL_REF(RogueClassTableEntry_Int32_String_*,entry_2,(((RogueClassTableEntry_Int32_String_*)(THIS->bins->as_objects[((hash_1) & (THIS->bin_mask))]))));
  while (ROGUE_COND(!!(entry_2)))
  {
    ROGUE_GC_CHECK;
    if (ROGUE_COND(((((entry_2->hash) == (hash_1))) && (((entry_2->key) == (key_0))))))
    {
      return (RogueClassTableEntry_Int32_String_*)(entry_2);
    }
    entry_2 = ((RogueClassTableEntry_Int32_String_*)(entry_2->adjacent_entry));
  }
  return (RogueClassTableEntry_Int32_String_*)(((RogueClassTableEntry_Int32_String_*)(NULL)));
}

RogueString* RogueTable_Int32_String___get__Int32( RogueClassTable_Int32_String_* THIS, RogueInt32 key_0 )
{
  ROGUE_GC_CHECK;
  ROGUE_DEF_LOCAL_REF(RogueClassTableEntry_Int32_String_*,entry_1,(((RogueClassTableEntry_Int32_String_*)(RogueTable_Int32_String___find__Int32( ROGUE_ARG(THIS), key_0 )))));
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

RogueStringBuilder* RogueTable_Int32_String___print_to__StringBuilder( RogueClassTable_Int32_String_* THIS, RogueStringBuilder* buffer_0 )
{
  ROGUE_GC_CHECK;
  RogueStringBuilder__print__Character_Logical( buffer_0, (RogueCharacter)'{', true );
  ROGUE_DEF_LOCAL_REF(RogueClassTableEntry_Int32_String_*,cur_1,(THIS->first_entry));
  RogueInt32 i_2 = (0);
  while (ROGUE_COND(!!(cur_1)))
  {
    ROGUE_GC_CHECK;
    if (ROGUE_COND(((i_2) > (0))))
    {
      RogueStringBuilder__print__Character_Logical( buffer_0, (RogueCharacter)',', true );
    }
    RogueStringBuilder__print__Int32( buffer_0, ROGUE_ARG(cur_1->key) );
    RogueStringBuilder__print__Character_Logical( buffer_0, (RogueCharacter)':', true );
    RogueStringBuilder__print__String( buffer_0, ROGUE_ARG(cur_1->value) );
    cur_1 = ((RogueClassTableEntry_Int32_String_*)(cur_1->next_entry));
    ++i_2;
  }
  RogueStringBuilder__print__Character_Logical( buffer_0, (RogueCharacter)'}', true );
  return (RogueStringBuilder*)(buffer_0);
}

RogueClassTable_Int32_String_* RogueTable_Int32_String___set__Int32_String( RogueClassTable_Int32_String_* THIS, RogueInt32 key_0, RogueString* value_1 )
{
  ROGUE_GC_CHECK;
  ROGUE_DEF_LOCAL_REF(RogueClassTableEntry_Int32_String_*,entry_2,(((RogueClassTableEntry_Int32_String_*)(RogueTable_Int32_String___find__Int32( ROGUE_ARG(THIS), key_0 )))));
  if (ROGUE_COND(!!(entry_2)))
  {
    entry_2->value = value_1;
    if (ROGUE_COND(!!(THIS->sort_function)))
    {
      RogueTable_Int32_String____adjust_entry_order__TableEntry_Int32_String_( ROGUE_ARG(THIS), entry_2 );
    }
    return (RogueClassTable_Int32_String_*)(THIS);
  }
  if (ROGUE_COND(((THIS->count) >= (THIS->bins->count))))
  {
    RogueTable_Int32_String____grow( ROGUE_ARG(THIS) );
  }
  RogueInt32 hash_3 = (((RogueInt32__hash_code( key_0 ))));
  RogueInt32 index_4 = (((hash_3) & (THIS->bin_mask)));
  if (ROGUE_COND(!(!!(entry_2))))
  {
    entry_2 = ((RogueClassTableEntry_Int32_String_*)(((RogueClassTableEntry_Int32_String_*)(RogueTableEntry_Int32_String___init__Int32_String_Int32( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassTableEntry_Int32_String_*,ROGUE_CREATE_OBJECT(TableEntry_Int32_String_))), key_0, value_1, hash_3 )))));
  }
  entry_2->adjacent_entry = ((RogueClassTableEntry_Int32_String_*)(THIS->bins->as_objects[index_4]));
  THIS->bins->as_objects[index_4] = entry_2;
  RogueTable_Int32_String____place_entry_in_order__TableEntry_Int32_String_( ROGUE_ARG(THIS), entry_2 );
  THIS->count = ((THIS->count) + (1));
  return (RogueClassTable_Int32_String_*)(THIS);
}

void RogueTable_Int32_String____adjust_entry_order__TableEntry_Int32_String_( RogueClassTable_Int32_String_* THIS, RogueClassTableEntry_Int32_String_* entry_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((void*)THIS->first_entry) == ((void*)THIS->last_entry)))
  {
    return;
  }
  if (ROGUE_COND(((void*)entry_0) == ((void*)THIS->first_entry)))
  {
    if (ROGUE_COND(((Rogue_Function_TableEntry_Int32_String__TableEntry_Int32_String__RETURNSLogical___call__TableEntry_Int32_String__TableEntry_Int32_String_( ROGUE_ARG(THIS->sort_function), entry_0, ROGUE_ARG(entry_0->next_entry) )))))
    {
      return;
    }
  }
  else if (ROGUE_COND(((void*)entry_0) == ((void*)THIS->last_entry)))
  {
    if (ROGUE_COND(((Rogue_Function_TableEntry_Int32_String__TableEntry_Int32_String__RETURNSLogical___call__TableEntry_Int32_String__TableEntry_Int32_String_( ROGUE_ARG(THIS->sort_function), ROGUE_ARG(entry_0->previous_entry), entry_0 )))))
    {
      return;
    }
  }
  else if (ROGUE_COND(((((Rogue_Function_TableEntry_Int32_String__TableEntry_Int32_String__RETURNSLogical___call__TableEntry_Int32_String__TableEntry_Int32_String_( ROGUE_ARG(THIS->sort_function), ROGUE_ARG(entry_0->previous_entry), entry_0 )))) && (((Rogue_Function_TableEntry_Int32_String__TableEntry_Int32_String__RETURNSLogical___call__TableEntry_Int32_String__TableEntry_Int32_String_( ROGUE_ARG(THIS->sort_function), entry_0, ROGUE_ARG(entry_0->next_entry) )))))))
  {
    return;
  }
  RogueTable_Int32_String____unlink__TableEntry_Int32_String_( ROGUE_ARG(THIS), entry_0 );
  RogueTable_Int32_String____place_entry_in_order__TableEntry_Int32_String_( ROGUE_ARG(THIS), entry_0 );
}

void RogueTable_Int32_String____place_entry_in_order__TableEntry_Int32_String_( RogueClassTable_Int32_String_* THIS, RogueClassTableEntry_Int32_String_* entry_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(!!(THIS->first_entry)))
  {
    if (ROGUE_COND(!!(THIS->sort_function)))
    {
      if (ROGUE_COND(((Rogue_Function_TableEntry_Int32_String__TableEntry_Int32_String__RETURNSLogical___call__TableEntry_Int32_String__TableEntry_Int32_String_( ROGUE_ARG(THIS->sort_function), entry_0, ROGUE_ARG(THIS->first_entry) )))))
      {
        entry_0->next_entry = THIS->first_entry;
        THIS->first_entry->previous_entry = entry_0;
        THIS->first_entry = entry_0;
      }
      else if (ROGUE_COND(((Rogue_Function_TableEntry_Int32_String__TableEntry_Int32_String__RETURNSLogical___call__TableEntry_Int32_String__TableEntry_Int32_String_( ROGUE_ARG(THIS->sort_function), ROGUE_ARG(THIS->last_entry), entry_0 )))))
      {
        THIS->last_entry->next_entry = entry_0;
        entry_0->previous_entry = THIS->last_entry;
        THIS->last_entry = entry_0;
      }
      else
      {
        ROGUE_DEF_LOCAL_REF(RogueClassTableEntry_Int32_String_*,cur_1,(THIS->first_entry));
        while (ROGUE_COND(!!(cur_1->next_entry)))
        {
          ROGUE_GC_CHECK;
          if (ROGUE_COND(((Rogue_Function_TableEntry_Int32_String__TableEntry_Int32_String__RETURNSLogical___call__TableEntry_Int32_String__TableEntry_Int32_String_( ROGUE_ARG(THIS->sort_function), entry_0, ROGUE_ARG(cur_1->next_entry) )))))
          {
            entry_0->previous_entry = cur_1;
            entry_0->next_entry = cur_1->next_entry;
            entry_0->next_entry->previous_entry = entry_0;
            cur_1->next_entry = entry_0;
            goto _auto_929;
          }
          cur_1 = ((RogueClassTableEntry_Int32_String_*)(cur_1->next_entry));
        }
        _auto_929:;
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

void RogueTable_Int32_String____unlink__TableEntry_Int32_String_( RogueClassTable_Int32_String_* THIS, RogueClassTableEntry_Int32_String_* entry_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((void*)entry_0) == ((void*)THIS->first_entry)))
  {
    if (ROGUE_COND(((void*)entry_0) == ((void*)THIS->last_entry)))
    {
      THIS->first_entry = ((RogueClassTableEntry_Int32_String_*)(NULL));
      THIS->last_entry = ((RogueClassTableEntry_Int32_String_*)(NULL));
    }
    else
    {
      THIS->first_entry = entry_0->next_entry;
      THIS->first_entry->previous_entry = ((RogueClassTableEntry_Int32_String_*)(NULL));
    }
  }
  else if (ROGUE_COND(((void*)entry_0) == ((void*)THIS->last_entry)))
  {
    THIS->last_entry = entry_0->previous_entry;
    THIS->last_entry->next_entry = ((RogueClassTableEntry_Int32_String_*)(NULL));
  }
  else
  {
    entry_0->previous_entry->next_entry = entry_0->next_entry;
    entry_0->next_entry->previous_entry = entry_0->previous_entry;
  }
}

void RogueTable_Int32_String____grow( RogueClassTable_Int32_String_* THIS )
{
  ROGUE_GC_CHECK;
  THIS->bins = RogueType_create_array( ((THIS->bins->count) * (2)), sizeof(RogueClassTableEntry_Int32_String_*), true, 41 );
  THIS->bin_mask = ((((THIS->bin_mask) << (1))) | (1));
  ROGUE_DEF_LOCAL_REF(RogueClassTableEntry_Int32_String_*,cur_0,(THIS->first_entry));
  while (ROGUE_COND(!!(cur_0)))
  {
    ROGUE_GC_CHECK;
    RogueInt32 index_1 = (((cur_0->hash) & (THIS->bin_mask)));
    cur_0->adjacent_entry = ((RogueClassTableEntry_Int32_String_*)(THIS->bins->as_objects[index_1]));
    THIS->bins->as_objects[index_1] = cur_0;
    cur_0 = ((RogueClassTableEntry_Int32_String_*)(cur_0->next_entry));
  }
}

RogueString* RogueArray_TableEntry_Int32_String____type_name( RogueArray* THIS )
{
  return (RogueString*)(Rogue_literal_strings[201]);
}

RogueClassTableEntry_Int32_String_* RogueTableEntry_Int32_String___init_object( RogueClassTableEntry_Int32_String_* THIS )
{
  ROGUE_GC_CHECK;
  RogueObject__init_object( ROGUE_ARG(((RogueObject*)THIS)) );
  return (RogueClassTableEntry_Int32_String_*)(THIS);
}

RogueString* RogueTableEntry_Int32_String___to_String( RogueClassTableEntry_Int32_String_* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[8] )))), ROGUE_ARG(((RogueString*)(RogueString__operatorPLUS__Int32( ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], Rogue_literal_strings[0] ))), ROGUE_ARG(THIS->key) )))) )))), Rogue_literal_strings[76] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], ROGUE_ARG(THIS->value) ))) )))), Rogue_literal_strings[11] )))) ))));
}

RogueString* RogueTableEntry_Int32_String___type_name( RogueClassTableEntry_Int32_String_* THIS )
{
  return (RogueString*)(Rogue_literal_strings[168]);
}

RogueClassTableEntry_Int32_String_* RogueTableEntry_Int32_String___init__Int32_String_Int32( RogueClassTableEntry_Int32_String_* THIS, RogueInt32 _key_0, RogueString* _value_1, RogueInt32 _hash_2 )
{
  ROGUE_GC_CHECK;
  THIS->key = _key_0;
  THIS->value = _value_1;
  THIS->hash = _hash_2;
  return (RogueClassTableEntry_Int32_String_*)(THIS);
}

RogueClass_Function_TableEntry_Int32_String__TableEntry_Int32_String__RETURNSLogical_* Rogue_Function_TableEntry_Int32_String__TableEntry_Int32_String__RETURNSLogical___init_object( RogueClass_Function_TableEntry_Int32_String__TableEntry_Int32_String__RETURNSLogical_* THIS )
{
  ROGUE_GC_CHECK;
  RogueObject__init_object( ROGUE_ARG(((RogueObject*)THIS)) );
  return (RogueClass_Function_TableEntry_Int32_String__TableEntry_Int32_String__RETURNSLogical_*)(THIS);
}

RogueString* Rogue_Function_TableEntry_Int32_String__TableEntry_Int32_String__RETURNSLogical___type_name( RogueClass_Function_TableEntry_Int32_String__TableEntry_Int32_String__RETURNSLogical_* THIS )
{
  return (RogueString*)(Rogue_literal_strings[169]);
}

RogueLogical Rogue_Function_TableEntry_Int32_String__TableEntry_Int32_String__RETURNSLogical___call__TableEntry_Int32_String__TableEntry_Int32_String_( RogueClass_Function_TableEntry_Int32_String__TableEntry_Int32_String__RETURNSLogical_* THIS, RogueClassTableEntry_Int32_String_* param1_0, RogueClassTableEntry_Int32_String_* param2_1 )
{
  return (RogueLogical)(false);
}

RogueString* RogueArray_MethodInfo___type_name( RogueArray* THIS )
{
  return (RogueString*)(Rogue_literal_strings[202]);
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
    ROGUE_DEF_LOCAL_REF(RogueCharacter_List*,_auto_934_0,(THIS));
    RogueInt32 _auto_935_0 = (0);
    RogueInt32 _auto_936_0 = (((_auto_934_0->count) - (1)));
    for (;ROGUE_COND(((_auto_935_0) <= (_auto_936_0)));++_auto_935_0)
    {
      ROGUE_GC_CHECK;
      RogueCharacter value_0 = (_auto_934_0->data->as_characters[_auto_935_0]);
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
  return (RogueString*)(Rogue_literal_strings[209]);
}

RogueCharacter_List* RogueCharacter_List__init__Int32( RogueCharacter_List* THIS, RogueInt32 initial_capacity_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(!!(initial_capacity_0)))
  {
    THIS->data = RogueType_create_array( initial_capacity_0, sizeof(RogueCharacter), false, 32 );
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
    THIS->data = RogueType_create_array( required_capacity_1, sizeof(RogueCharacter), false, 32 );
  }
  else if (ROGUE_COND(((required_capacity_1) > (THIS->data->count))))
  {
    RogueInt32 cap_2 = (((RogueCharacter_List__capacity( ROGUE_ARG(THIS) ))));
    if (ROGUE_COND(((required_capacity_1) < (((cap_2) + (cap_2))))))
    {
      required_capacity_1 = ((RogueInt32)(((cap_2) + (cap_2))));
    }
    ROGUE_DEF_LOCAL_REF(RogueArray*,new_data_3,(RogueType_create_array( required_capacity_1, sizeof(RogueCharacter), false, 32 )));
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
  return (RogueString*)(Rogue_literal_strings[203]);
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
  return (RogueString*)(Rogue_literal_strings[177]);
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
    ROGUE_DEF_LOCAL_REF(RogueValue_List*,_auto_1022_0,(THIS->data));
    RogueInt32 _auto_1023_0 = (0);
    RogueInt32 _auto_1024_0 = (((_auto_1022_0->count) - (1)));
    for (;ROGUE_COND(((_auto_1023_0) <= (_auto_1024_0)));++_auto_1023_0)
    {
      ROGUE_GC_CHECK;
      ROGUE_DEF_LOCAL_REF(RogueClassValue*,existing_0,(((RogueClassValue*)(_auto_1022_0->data->as_objects[_auto_1023_0]))));
      if (ROGUE_COND((((((RogueOptionalValue__operator__Value( existing_0 ))) && ((Rogue_call_ROGUEM9( 49, existing_0 ))))) && ((RogueString__operatorEQUALSEQUALS__String_String( ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ((RogueObject*)existing_0) ))), value_0 ))))))
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
    ROGUE_DEF_LOCAL_REF(RogueValue_List*,_auto_1025_0,(THIS->data));
    RogueInt32 _auto_1026_0 = (0);
    RogueInt32 _auto_1027_0 = (((_auto_1025_0->count) - (1)));
    for (;ROGUE_COND(((_auto_1026_0) <= (_auto_1027_0)));++_auto_1026_0)
    {
      ROGUE_GC_CHECK;
      ROGUE_DEF_LOCAL_REF(RogueClassValue*,existing_0,(((RogueClassValue*)(_auto_1025_0->data->as_objects[_auto_1026_0]))));
      if (ROGUE_COND((((RogueOptionalValue__operator__Value( existing_0 ))) && ((Rogue_call_ROGUEM6( 59, existing_0, value_0 ))))))
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
    ROGUE_DEF_LOCAL_REF(RogueValue_List*,_auto_1054_0,(THIS->data));
    RogueInt32 _auto_1055_0 = (0);
    RogueInt32 _auto_1056_0 = (((_auto_1054_0->count) - (1)));
    for (;ROGUE_COND(((_auto_1055_0) <= (_auto_1056_0)));++_auto_1055_0)
    {
      ROGUE_GC_CHECK;
      ROGUE_DEF_LOCAL_REF(RogueClassValue*,value_0,(((RogueClassValue*)(_auto_1054_0->data->as_objects[_auto_1055_0]))));
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
      if (ROGUE_COND(((((void*)value_0) != ((void*)NULL)) && ((((RogueOptionalValue__operator__Value( value_0 ))) || ((Rogue_call_ROGUEM9( 43, value_0 ))))))))
      {
        Rogue_call_ROGUEM17( 115, value_0, buffer_0, flags_1 );
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
    ROGUE_DEF_LOCAL_REF(RogueValue_List*,_auto_1057_0,(THIS));
    RogueInt32 _auto_1058_0 = (0);
    RogueInt32 _auto_1059_0 = (((_auto_1057_0->count) - (1)));
    for (;ROGUE_COND(((_auto_1058_0) <= (_auto_1059_0)));++_auto_1058_0)
    {
      ROGUE_GC_CHECK;
      ROGUE_DEF_LOCAL_REF(RogueClassValue*,value_0,(((RogueClassValue*)(_auto_1057_0->data->as_objects[_auto_1058_0]))));
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
  return (RogueString*)(Rogue_literal_strings[210]);
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
        ROGUE_DEF_LOCAL_REF(RogueValue_List*,_auto_1101_0,(THIS));
        RogueInt32 i_0 = (0);
        RogueInt32 _auto_1102_0 = (((_auto_1101_0->count) - (1)));
        for (;ROGUE_COND(((i_0) <= (_auto_1102_0)));++i_0)
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
    ROGUE_DEF_LOCAL_REF(RogueValue_List*,_auto_1103_0,(THIS));
    RogueInt32 i_0 = (0);
    RogueInt32 _auto_1104_0 = (((_auto_1103_0->count) - (1)));
    for (;ROGUE_COND(((i_0) <= (_auto_1104_0)));++i_0)
    {
      ROGUE_GC_CHECK;
      if (ROGUE_COND((Rogue_call_ROGUEM6( 59, value_0, ROGUE_ARG(((RogueClassValue*)(THIS->data->as_objects[i_0]))) ))))
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
  return (RogueString*)(Rogue_literal_strings[204]);
}

RogueClassObjectValue* RogueObjectValue__init_object( RogueClassObjectValue* THIS )
{
  ROGUE_GC_CHECK;
  RogueValue__init_object( ROGUE_ARG(((RogueClassValue*)THIS)) );
  return (RogueClassObjectValue*)(THIS);
}

RogueString* RogueObjectValue__to_String( RogueClassObjectValue* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(THIS->value) )));
}

RogueString* RogueObjectValue__type_name( RogueClassObjectValue* THIS )
{
  return (RogueString*)(Rogue_literal_strings[232]);
}

RogueLogical RogueObjectValue__is_object( RogueClassObjectValue* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueLogical)(true);
}

RogueObject* RogueObjectValue__to_Object( RogueClassObjectValue* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueObject*)(THIS->value);
}

RogueStringBuilder* RogueObjectValue__to_json__StringBuilder_Int32( RogueClassObjectValue* THIS, RogueStringBuilder* buffer_0, RogueInt32 flags_1 )
{
  ROGUE_GC_CHECK;
  RogueStringBuilder__print__String( buffer_0, ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(THIS->value) ))) );
  return (RogueStringBuilder*)(buffer_0);
}

RogueClassObjectValue* RogueObjectValue__init__Object( RogueClassObjectValue* THIS, RogueObject* _auto_129_0 )
{
  ROGUE_GC_CHECK;
  THIS->value = _auto_129_0;
  return (RogueClassObjectValue*)(THIS);
}

RogueClassStringConsolidationTable* RogueStringConsolidationTable__init_object( RogueClassStringConsolidationTable* THIS )
{
  ROGUE_GC_CHECK;
  RogueStringTable_String___init_object( ROGUE_ARG(((RogueClassStringTable_String_*)THIS)) );
  return (RogueClassStringConsolidationTable*)(THIS);
}

RogueString* RogueStringConsolidationTable__type_name( RogueClassStringConsolidationTable* THIS )
{
  return (RogueString*)(Rogue_literal_strings[226]);
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
  return (RogueString*)(Rogue_literal_strings[219]);
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
  return (RogueString*)(Rogue_literal_strings[146]);
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
  THIS->bins = RogueType_create_array( bin_count_0, sizeof(RogueClassTableEntry_String_String_*), true, 54 );
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
            goto _auto_1172;
          }
          cur_1 = ((RogueClassTableEntry_String_String_*)(cur_1->next_entry));
        }
        _auto_1172:;
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
  THIS->bins = RogueType_create_array( ((THIS->bins->count) * (2)), sizeof(RogueClassTableEntry_String_String_*), true, 54 );
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
  return (RogueString*)(Rogue_literal_strings[205]);
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
  return (RogueString*)(Rogue_literal_strings[170]);
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
  return (RogueString*)(Rogue_literal_strings[171]);
}

RogueLogical Rogue_Function_TableEntry_String_String__TableEntry_String_String__RETURNSLogical___call__TableEntry_String_String__TableEntry_String_String_( RogueClass_Function_TableEntry_String_String__TableEntry_String_String__RETURNSLogical_* THIS, RogueClassTableEntry_String_String_* param1_0, RogueClassTableEntry_String_String_* param2_1 )
{
  return (RogueLogical)(false);
}

RogueClassReader_Character_* RogueReader_Character___close( RogueObject* THIS )
{
  switch (THIS->type->index)
  {
    case 58:
      return (RogueClassReader_Character_*)RogueConsole__close( (RogueClassConsole*)THIS );
    case 72:
      return (RogueClassReader_Character_*)RogueScanner__close( (RogueClassScanner*)THIS );
    case 97:
      return (RogueClassReader_Character_*)RogueUTF8Reader__close( (RogueClassUTF8Reader*)THIS );
    default:
      return 0;
  }
}

RogueLogical RogueReader_Character___has_another( RogueObject* THIS )
{
  switch (THIS->type->index)
  {
    case 58:
      return RogueConsole__has_another( (RogueClassConsole*)THIS );
    case 72:
      return RogueScanner__has_another( (RogueClassScanner*)THIS );
    case 97:
      return RogueUTF8Reader__has_another( (RogueClassUTF8Reader*)THIS );
    default:
      return 0;
  }
}

RogueCharacter RogueReader_Character___read( RogueObject* THIS )
{
  switch (THIS->type->index)
  {
    case 58:
      return RogueConsole__read( (RogueClassConsole*)THIS );
    case 72:
      return RogueScanner__read( (RogueClassScanner*)THIS );
    case 97:
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

  RogueGlobal__on_exit___Function___( ((RogueClassGlobal*)ROGUE_SINGLETON(Global)), ROGUE_ARG(((RogueClass_Function___*)(((RogueClassFunction_1182*)(RogueFunction_1182__init__Console( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassFunction_1182*,ROGUE_CREATE_OBJECT(Function_1182))), ROGUE_ARG(THIS) )))))) );
  return (RogueClassConsole*)(THIS);
}

RogueString* RogueConsole__type_name( RogueClassConsole* THIS )
{
  return (RogueString*)(Rogue_literal_strings[148]);
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
            if ( !(THIS->decode_bytes) ) goto _auto_1185;
            if ( !(THIS->input_bytes->count) ) goto _auto_1185;
            if (ROGUE_COND(((((RogueInt32)(b1_0))) == (27))))
            {
              if ( !(((((THIS->input_bytes->count) >= (2))) && (((((RogueInt32)(THIS->input_bytes->data->as_bytes[0]))) == (91))))) ) goto _auto_1185;
              RogueByte_List__remove_at__Int32( ROGUE_ARG(THIS->input_bytes), 0 );
              THIS->next_input_character = RogueOptionalInt32( ((((((RogueInt32)(((RogueByte_List__remove_at__Int32( ROGUE_ARG(THIS->input_bytes), 0 )))))) - (65))) + (17)), true );
            }
            else
            {
              if ( !(((((RogueInt32)(b1_0))) >= (192))) ) goto _auto_1185;
              RogueInt32 result_1 = 0;
              if (ROGUE_COND(((((((RogueInt32)(b1_0))) & (224))) == (192))))
              {
                if ( !(((THIS->input_bytes->count) >= (1))) ) goto _auto_1185;
                result_1 = ((RogueInt32)(((((RogueInt32)(b1_0))) & (31))));
                result_1 = ((RogueInt32)(((((result_1) << (6))) | (((((RogueInt32)(((RogueByte_List__remove_at__Int32( ROGUE_ARG(THIS->input_bytes), 0 )))))) & (63))))));
              }
              else if (ROGUE_COND(((((((RogueInt32)(b1_0))) & (240))) == (224))))
              {
                if ( !(((THIS->input_bytes->count) >= (2))) ) goto _auto_1185;
                result_1 = ((RogueInt32)(((((RogueInt32)(b1_0))) & (15))));
                result_1 = ((RogueInt32)(((((result_1) << (6))) | (((((RogueInt32)(((RogueByte_List__remove_at__Int32( ROGUE_ARG(THIS->input_bytes), 0 )))))) & (63))))));
                result_1 = ((RogueInt32)(((((result_1) << (6))) | (((((RogueInt32)(((RogueByte_List__remove_at__Int32( ROGUE_ARG(THIS->input_bytes), 0 )))))) & (63))))));
              }
              else if (ROGUE_COND(((((((RogueInt32)(b1_0))) & (248))) == (240))))
              {
                if ( !(((THIS->input_bytes->count) >= (3))) ) goto _auto_1185;
                result_1 = ((RogueInt32)(((((RogueInt32)(b1_0))) & (7))));
                result_1 = ((RogueInt32)(((((result_1) << (6))) | (((((RogueInt32)(((RogueByte_List__remove_at__Int32( ROGUE_ARG(THIS->input_bytes), 0 )))))) & (63))))));
                result_1 = ((RogueInt32)(((((result_1) << (6))) | (((((RogueInt32)(((RogueByte_List__remove_at__Int32( ROGUE_ARG(THIS->input_bytes), 0 )))))) & (63))))));
                result_1 = ((RogueInt32)(((((result_1) << (6))) | (((((RogueInt32)(((RogueByte_List__remove_at__Int32( ROGUE_ARG(THIS->input_bytes), 0 )))))) & (63))))));
              }
              else if (ROGUE_COND(((((((RogueInt32)(b1_0))) & (252))) == (248))))
              {
                if ( !(((THIS->input_bytes->count) >= (4))) ) goto _auto_1185;
                result_1 = ((RogueInt32)(((((RogueInt32)(b1_0))) & (3))));
                result_1 = ((RogueInt32)(((((result_1) << (6))) | (((((RogueInt32)(((RogueByte_List__remove_at__Int32( ROGUE_ARG(THIS->input_bytes), 0 )))))) & (63))))));
                result_1 = ((RogueInt32)(((((result_1) << (6))) | (((((RogueInt32)(((RogueByte_List__remove_at__Int32( ROGUE_ARG(THIS->input_bytes), 0 )))))) & (63))))));
                result_1 = ((RogueInt32)(((((result_1) << (6))) | (((((RogueInt32)(((RogueByte_List__remove_at__Int32( ROGUE_ARG(THIS->input_bytes), 0 )))))) & (63))))));
                result_1 = ((RogueInt32)(((((result_1) << (6))) | (((((RogueInt32)(((RogueByte_List__remove_at__Int32( ROGUE_ARG(THIS->input_bytes), 0 )))))) & (63))))));
              }
              else
              {
                if ( !(((THIS->input_bytes->count) >= (5))) ) goto _auto_1185;
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
          goto _auto_1184;
        }
        _auto_1185:;
        {
          THIS->next_input_character = RogueOptionalInt32( ((RogueInt32)(b1_0)), true );
          }
      }
      _auto_1184:;
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
    case 58:
      return RogueConsole__close( (RogueClassConsole*)THIS );
    case 60:
      return (RogueClassPrintWriter_output_buffer_*)RogueConsoleErrorPrinter__close( (RogueClassConsoleErrorPrinter*)THIS );
    default:
      return 0;
  }
}

RogueClassPrintWriter_output_buffer_* RoguePrintWriter_output_buffer___flush( RogueObject* THIS )
{
  switch (THIS->type->index)
  {
    case 58:
      return (RogueClassPrintWriter_output_buffer_*)RogueConsole__flush( (RogueClassConsole*)THIS );
    case 60:
      return (RogueClassPrintWriter_output_buffer_*)RogueConsoleErrorPrinter__flush( (RogueClassConsoleErrorPrinter*)THIS );
    default:
      return 0;
  }
}

RogueClassPrintWriter_output_buffer_* RoguePrintWriter_output_buffer___println__String( RogueObject* THIS, RogueString* value_0 )
{
  switch (THIS->type->index)
  {
    case 58:
      return (RogueClassPrintWriter_output_buffer_*)RogueConsole__println__String( (RogueClassConsole*)THIS, value_0 );
    case 60:
      return (RogueClassPrintWriter_output_buffer_*)RogueConsoleErrorPrinter__println__String( (RogueClassConsoleErrorPrinter*)THIS, value_0 );
    default:
      return 0;
  }
}

RogueClassPrintWriter_output_buffer_* RoguePrintWriter_output_buffer___write__StringBuilder( RogueObject* THIS, RogueStringBuilder* buffer_0 )
{
  switch (THIS->type->index)
  {
    case 58:
      return (RogueClassPrintWriter_output_buffer_*)RogueConsole__write__StringBuilder( (RogueClassConsole*)THIS, buffer_0 );
    case 60:
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
  return (RogueString*)(Rogue_literal_strings[159]);
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
  return (RogueString*)(Rogue_literal_strings[206]);
}

RogueClassMath* RogueMath__init_object( RogueClassMath* THIS )
{
  ROGUE_GC_CHECK;
  RogueObject__init_object( ROGUE_ARG(((RogueObject*)THIS)) );
  return (RogueClassMath*)(THIS);
}

RogueString* RogueMath__type_name( RogueClassMath* THIS )
{
  return (RogueString*)(Rogue_literal_strings[149]);
}

RogueClassBoxed* RogueBoxed__init_object( RogueClassBoxed* THIS )
{
  ROGUE_GC_CHECK;
  RogueObject__init_object( ROGUE_ARG(((RogueObject*)THIS)) );
  return (RogueClassBoxed*)(THIS);
}

RogueString* RogueBoxed__type_name( RogueClassBoxed* THIS )
{
  return (RogueString*)(Rogue_literal_strings[231]);
}

RogueClassFunction_221* RogueFunction_221__init_object( RogueClassFunction_221* THIS )
{
  ROGUE_GC_CHECK;
  Rogue_Function_____init_object( ROGUE_ARG(((RogueClass_Function___*)THIS)) );
  return (RogueClassFunction_221*)(THIS);
}

RogueString* RogueFunction_221__type_name( RogueClassFunction_221* THIS )
{
  return (RogueString*)(Rogue_literal_strings[216]);
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
  return (RogueString*)(Rogue_literal_strings[151]);
}

RogueClassError* RogueError__init_object( RogueClassError* THIS )
{
  ROGUE_GC_CHECK;
  RogueException__init_object( ROGUE_ARG(((RogueException*)THIS)) );
  return (RogueClassError*)(THIS);
}

RogueString* RogueError__type_name( RogueClassError* THIS )
{
  return (RogueString*)(Rogue_literal_strings[218]);
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
  return (RogueString*)(Rogue_literal_strings[152]);
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
  return (RogueString*)(Rogue_literal_strings[153]);
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
    case 96:
      return (RogueClassReader_Byte_*)RogueFileReader__close( (RogueClassFileReader*)THIS );
    default:
      return 0;
  }
}

RogueLogical RogueReader_Byte___has_another( RogueObject* THIS )
{
  switch (THIS->type->index)
  {
    case 96:
      return RogueFileReader__has_another( (RogueClassFileReader*)THIS );
    default:
      return 0;
  }
}

RogueByte RogueReader_Byte___read( RogueObject* THIS )
{
  switch (THIS->type->index)
  {
    case 96:
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
  return (RogueString*)(Rogue_literal_strings[154]);
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
      ROGUE_DEF_LOCAL_REF(RogueByte_List*,_auto_1237_0,(bytes_0));
      RogueInt32 _auto_1238_0 = (0);
      RogueInt32 _auto_1239_0 = (((_auto_1237_0->count) - (1)));
      for (;ROGUE_COND(((_auto_1238_0) <= (_auto_1239_0)));++_auto_1238_0)
      {
        ROGUE_GC_CHECK;
        RogueByte byte_0 = (_auto_1237_0->data->as_bytes[_auto_1238_0]);
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
    throw ((RogueClassIOError*)(RogueIOError___throw( ROGUE_ARG(((RogueClassIOError*)(((RogueException*)Rogue_call_ROGUEM3( 13, ROGUE_ARG(((RogueException*)ROGUE_CREATE_REF(RogueClassIOError*,ROGUE_CREATE_OBJECT(IOError)))), ROGUE_ARG(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[24] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], _filepath_0 ))) )))), Rogue_literal_strings[48] )))) )))) ))))) )));
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
    case 70:
      return (RogueClassWriter_Byte_*)RogueFileWriter__close( (RogueClassFileWriter*)THIS );
    default:
      return 0;
  }
}

RogueClassWriter_Byte_* RogueWriter_Byte___flush( RogueObject* THIS )
{
  switch (THIS->type->index)
  {
    case 70:
      return (RogueClassWriter_Byte_*)RogueFileWriter__flush( (RogueClassFileWriter*)THIS );
    default:
      return 0;
  }
}

RogueClassWriter_Byte_* RogueWriter_Byte___write__Byte_List( RogueObject* THIS, RogueByte_List* list_0 )
{
  switch (THIS->type->index)
  {
    case 70:
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
  return (RogueString*)(Rogue_literal_strings[155]);
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

RogueClassScanner* RogueScanner__init__String_Int32_Logical( RogueClassScanner* THIS, RogueString* source_0, RogueInt32 _auto_249_1, RogueLogical preserve_crlf_2 )
{
  ROGUE_GC_CHECK;
  THIS->spaces_per_tab = _auto_249_1;
  RogueInt32 tab_count_3 = (0);
  if (ROGUE_COND(!!(THIS->spaces_per_tab)))
  {
    {
      ROGUE_DEF_LOCAL_REF(RogueString*,_auto_1240_0,(source_0));
      RogueInt32 _auto_1241_0 = (0);
      RogueInt32 _auto_1242_0 = (((_auto_1240_0->character_count) - (1)));
      for (;ROGUE_COND(((_auto_1241_0) <= (_auto_1242_0)));++_auto_1241_0)
      {
        ROGUE_GC_CHECK;
        RogueCharacter b_0 = (RogueString_character_at(_auto_1240_0,_auto_1241_0));
        if (ROGUE_COND(((b_0) == ((RogueCharacter)9))))
        {
          ++tab_count_3;
        }
      }
    }
  }
  THIS->data = ((RogueCharacter_List*)(RogueCharacter_List__init__Int32( ROGUE_ARG(ROGUE_CREATE_REF(RogueCharacter_List*,ROGUE_CREATE_OBJECT(Character_List))), ROGUE_ARG(((source_0->character_count) + (tab_count_3))) )));
  {
    ROGUE_DEF_LOCAL_REF(RogueString*,_auto_1243_0,(source_0));
    RogueInt32 _auto_1244_0 = (0);
    RogueInt32 _auto_1245_0 = (((_auto_1243_0->character_count) - (1)));
    for (;ROGUE_COND(((_auto_1244_0) <= (_auto_1245_0)));++_auto_1244_0)
    {
      ROGUE_GC_CHECK;
      RogueCharacter b_0 = (RogueString_character_at(_auto_1243_0,_auto_1244_0));
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
  return (RogueString*)(Rogue_literal_strings[156]);
}

RogueClassJSONParser* RogueJSONParser__init__String( RogueClassJSONParser* THIS, RogueString* json_0 )
{
  ROGUE_GC_CHECK;
  THIS->reader = ((RogueClassScanner*)(RogueScanner__init__String_Int32_Logical( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassScanner*,ROGUE_CREATE_OBJECT(Scanner))), json_0, 0, false )));
  return (RogueClassJSONParser*)(THIS);
}

RogueClassJSONParser* RogueJSONParser__init__Scanner( RogueClassJSONParser* THIS, RogueClassScanner* _auto_255_0 )
{
  ROGUE_GC_CHECK;
  THIS->reader = _auto_255_0;
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
      goto _auto_1256;
    }
    RogueValueList__add__Value( list_2, ROGUE_ARG(((RogueClassValue*)(RogueJSONParser__parse_value( ROGUE_ARG(THIS) )))) );
    RogueJSONParser__consume_spaces_and_eols( ROGUE_ARG(THIS) );
  }
  _auto_1256:;
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
    RogueInt32 _auto_254_2 = (4);
    for (;ROGUE_COND(((i_1) <= (_auto_254_2)));++i_1)
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
  return (RogueString*)(Rogue_literal_strings[158]);
}

RogueClass_Function_String_RETURNSString_* Rogue_Function_String_RETURNSString___init_object( RogueClass_Function_String_RETURNSString_* THIS )
{
  ROGUE_GC_CHECK;
  RogueObject__init_object( ROGUE_ARG(((RogueObject*)THIS)) );
  return (RogueClass_Function_String_RETURNSString_*)(THIS);
}

RogueString* Rogue_Function_String_RETURNSString___type_name( RogueClass_Function_String_RETURNSString_* THIS )
{
  return (RogueString*)(Rogue_literal_strings[160]);
}

RogueString* Rogue_Function_String_RETURNSString___call__String( RogueClass_Function_String_RETURNSString_* THIS, RogueString* param1_0 )
{
  return (RogueString*)(((RogueString*)(NULL)));
}

RogueClassFunction_281* RogueFunction_281__init_object( RogueClassFunction_281* THIS )
{
  ROGUE_GC_CHECK;
  Rogue_Function_String_RETURNSString___init_object( ROGUE_ARG(((RogueClass_Function_String_RETURNSString_*)THIS)) );
  return (RogueClassFunction_281*)(THIS);
}

RogueString* RogueFunction_281__type_name( RogueClassFunction_281* THIS )
{
  return (RogueString*)(Rogue_literal_strings[224]);
}

RogueString* RogueFunction_281__call__String( RogueClassFunction_281* THIS, RogueString* arg_0 )
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
  return (RogueString*)(Rogue_literal_strings[161]);
}

RogueWeakReference* RogueWeakReference__init_object( RogueWeakReference* THIS )
{
  ROGUE_GC_CHECK;
  RogueObject__init_object( ROGUE_ARG(((RogueObject*)THIS)) );
  return (RogueWeakReference*)(THIS);
}

RogueString* RogueWeakReference__type_name( RogueWeakReference* THIS )
{
  return (RogueString*)(Rogue_literal_strings[163]);
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
  return (RogueString*)(Rogue_literal_strings[172]);
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

RogueClassPrintWriterAdapter* RoguePrintWriterAdapter__init__Writer_Byte_( RogueClassPrintWriterAdapter* THIS, RogueClassWriter_Byte_* _auto_325_0 )
{
  ROGUE_GC_CHECK;
  THIS->output = _auto_325_0;
  return (RogueClassPrintWriterAdapter*)(THIS);
}

RogueClassPrintWriter_buffer_* RoguePrintWriter_buffer___close( RogueObject* THIS )
{
  switch (THIS->type->index)
  {
    case 79:
      return (RogueClassPrintWriter_buffer_*)RoguePrintWriterAdapter__close( (RogueClassPrintWriterAdapter*)THIS );
    default:
      return 0;
  }
}

RogueClassPrintWriter_buffer_* RoguePrintWriter_buffer___flush( RogueObject* THIS )
{
  switch (THIS->type->index)
  {
    case 79:
      return (RogueClassPrintWriter_buffer_*)RoguePrintWriterAdapter__flush( (RogueClassPrintWriterAdapter*)THIS );
    default:
      return 0;
  }
}

RogueClassPrintWriter_buffer_* RoguePrintWriter_buffer___println__String( RogueObject* THIS, RogueString* value_0 )
{
  switch (THIS->type->index)
  {
    case 79:
      return (RogueClassPrintWriter_buffer_*)RoguePrintWriterAdapter__println__String( (RogueClassPrintWriterAdapter*)THIS, value_0 );
    default:
      return 0;
  }
}

RogueClassPrintWriter_buffer_* RoguePrintWriter_buffer___write__StringBuilder( RogueObject* THIS, RogueStringBuilder* buffer_0 )
{
  switch (THIS->type->index)
  {
    case 79:
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
  return (RogueString*)(Rogue_literal_strings[179]);
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

RogueClassLogicalValue* RogueLogicalValue__init__Logical( RogueClassLogicalValue* THIS, RogueLogical _auto_326_0 )
{
  ROGUE_GC_CHECK;
  THIS->value = _auto_326_0;
  return (RogueClassLogicalValue*)(THIS);
}

RogueClassInt32Value* RogueInt32Value__init_object( RogueClassInt32Value* THIS )
{
  ROGUE_GC_CHECK;
  RogueValue__init_object( ROGUE_ARG(((RogueClassValue*)THIS)) );
  return (RogueClassInt32Value*)(THIS);
}

RogueString* RogueInt32Value__to_String( RogueClassInt32Value* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(((RogueString*)(RogueInt32__to_String( ROGUE_ARG(THIS->value) ))));
}

RogueString* RogueInt32Value__type_name( RogueClassInt32Value* THIS )
{
  return (RogueString*)(Rogue_literal_strings[233]);
}

RogueLogical RogueInt32Value__is_int32( RogueClassInt32Value* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueLogical)(true);
}

RogueLogical RogueInt32Value__is_number( RogueClassInt32Value* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueLogical)(true);
}

RogueLogical RogueInt32Value__operatorEQUALSEQUALS__Value( RogueClassInt32Value* THIS, RogueClassValue* other_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND((Rogue_call_ROGUEM9( 40, other_0 ))))
  {
    return (RogueLogical)(((THIS->value) == ((Rogue_call_ROGUEM7( 107, other_0 )))));
  }
  if (ROGUE_COND((Rogue_call_ROGUEM9( 41, other_0 ))))
  {
    return (RogueLogical)(((((RogueInt64)(THIS->value))) == ((Rogue_call_ROGUEM2( 106, other_0 )))));
  }
  if (ROGUE_COND((Rogue_call_ROGUEM9( 46, other_0 ))))
  {
    return (RogueLogical)(((((RogueReal64)(THIS->value))) == ((Rogue_call_ROGUEM13( 109, other_0 )))));
  }
  if (ROGUE_COND((Rogue_call_ROGUEM9( 43, other_0 ))))
  {
    return (RogueLogical)(((!!(THIS->value)) == ((Rogue_call_ROGUEM9( 108, other_0 )))));
  }
  else
  {
    return (RogueLogical)(false);
  }
}

RogueInt64 RogueInt32Value__to_Int64( RogueClassInt32Value* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueInt64)(((RogueInt64)(THIS->value)));
}

RogueInt32 RogueInt32Value__to_Int32( RogueClassInt32Value* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueInt32)(THIS->value);
}

RogueReal64 RogueInt32Value__to_Real64( RogueClassInt32Value* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueReal64)(((RogueReal64)(THIS->value)));
}

RogueStringBuilder* RogueInt32Value__to_json__StringBuilder_Int32( RogueClassInt32Value* THIS, RogueStringBuilder* buffer_0, RogueInt32 flags_1 )
{
  ROGUE_GC_CHECK;
  RogueStringBuilder__print__Int32( buffer_0, ROGUE_ARG(THIS->value) );
  return (RogueStringBuilder*)(buffer_0);
}

RogueClassInt32Value* RogueInt32Value__init__Int32( RogueClassInt32Value* THIS, RogueInt32 _auto_327_0 )
{
  ROGUE_GC_CHECK;
  THIS->value = _auto_327_0;
  return (RogueClassInt32Value*)(THIS);
}

RogueClassInt64Value* RogueInt64Value__init_object( RogueClassInt64Value* THIS )
{
  ROGUE_GC_CHECK;
  RogueValue__init_object( ROGUE_ARG(((RogueClassValue*)THIS)) );
  return (RogueClassInt64Value*)(THIS);
}

RogueString* RogueInt64Value__to_String( RogueClassInt64Value* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(((RogueString*)(RogueInt64__to_String( ROGUE_ARG(THIS->value) ))));
}

RogueString* RogueInt64Value__type_name( RogueClassInt64Value* THIS )
{
  return (RogueString*)(Rogue_literal_strings[234]);
}

RogueLogical RogueInt64Value__is_int64( RogueClassInt64Value* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueLogical)(true);
}

RogueLogical RogueInt64Value__is_number( RogueClassInt64Value* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueLogical)(true);
}

RogueLogical RogueInt64Value__operatorEQUALSEQUALS__Value( RogueClassInt64Value* THIS, RogueClassValue* other_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND((((Rogue_call_ROGUEM9( 41, other_0 ))) || ((Rogue_call_ROGUEM9( 40, other_0 ))))))
  {
    return (RogueLogical)(((THIS->value) == ((Rogue_call_ROGUEM2( 106, other_0 )))));
  }
  if (ROGUE_COND((Rogue_call_ROGUEM9( 46, other_0 ))))
  {
    return (RogueLogical)(((((RogueReal64)(THIS->value))) == ((Rogue_call_ROGUEM13( 109, other_0 )))));
  }
  if (ROGUE_COND((Rogue_call_ROGUEM9( 43, other_0 ))))
  {
    return (RogueLogical)(((!!(THIS->value)) == ((Rogue_call_ROGUEM9( 108, other_0 )))));
  }
  else
  {
    return (RogueLogical)(false);
  }
}

RogueInt64 RogueInt64Value__to_Int64( RogueClassInt64Value* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueInt64)(THIS->value);
}

RogueInt32 RogueInt64Value__to_Int32( RogueClassInt64Value* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueInt32)(((RogueInt32)(THIS->value)));
}

RogueReal64 RogueInt64Value__to_Real64( RogueClassInt64Value* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueReal64)(((RogueReal64)(THIS->value)));
}

RogueStringBuilder* RogueInt64Value__to_json__StringBuilder_Int32( RogueClassInt64Value* THIS, RogueStringBuilder* buffer_0, RogueInt32 flags_1 )
{
  ROGUE_GC_CHECK;
  RogueStringBuilder__print__Int64( buffer_0, ROGUE_ARG(THIS->value) );
  return (RogueStringBuilder*)(buffer_0);
}

RogueClassInt64Value* RogueInt64Value__init__Int64( RogueClassInt64Value* THIS, RogueInt64 _auto_328_0 )
{
  ROGUE_GC_CHECK;
  THIS->value = _auto_328_0;
  return (RogueClassInt64Value*)(THIS);
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
  return (RogueString*)(Rogue_literal_strings[180]);
}

RogueLogical RogueReal64Value__is_number( RogueClassReal64Value* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueLogical)(true);
}

RogueLogical RogueReal64Value__operatorEQUALSEQUALS__Value( RogueClassReal64Value* THIS, RogueClassValue* other_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND((Rogue_call_ROGUEM9( 46, other_0 ))))
  {
    return (RogueLogical)(((THIS->value) == ((Rogue_call_ROGUEM13( 109, other_0 )))));
  }
  if (ROGUE_COND((Rogue_call_ROGUEM9( 43, other_0 ))))
  {
    return (RogueLogical)(((((THIS->value) != (0.0))) == ((Rogue_call_ROGUEM9( 108, other_0 )))));
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

RogueClassReal64Value* RogueReal64Value__init__Real64( RogueClassReal64Value* THIS, RogueReal64 _auto_329_0 )
{
  ROGUE_GC_CHECK;
  THIS->value = _auto_329_0;
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
  return (RogueString*)(Rogue_literal_strings[181]);
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
  return (RogueLogical)(((((void*)other_0) == ((void*)NULL)) || ((Rogue_call_ROGUEM9( 44, other_0 )))));
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
  return (RogueString*)(Rogue_literal_strings[182]);
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
  if (ROGUE_COND((Rogue_call_ROGUEM9( 49, other_0 ))))
  {
    return (RogueLogical)((RogueString__operatorEQUALSEQUALS__String_String( ROGUE_ARG(THIS->value), ROGUE_ARG(((RogueString*)Rogue_call_ROGUEM1( 6, ((RogueObject*)other_0) ))) )));
  }
  else
  {
    return (RogueLogical)(false);
  }
}

RogueCharacter RogueStringValue__to_Character( RogueClassStringValue* THIS )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(((THIS->value->character_count) > (0))))
  {
    return (RogueCharacter)(RogueString_character_at(THIS->value,0));
  }
  else
  {
    return (RogueCharacter)((RogueCharacter)0);
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
  return (RogueLogical)((((((((((RogueString__operatorEQUALSEQUALS__String_String( ROGUE_ARG(THIS->value), Rogue_literal_strings[22] ))) || ((RogueString__operatorEQUALSEQUALS__String_String( ROGUE_ARG(THIS->value), Rogue_literal_strings[183] ))))) || ((RogueString__operatorEQUALSEQUALS__String_String( ROGUE_ARG(THIS->value), Rogue_literal_strings[184] ))))) || ((RogueString__operatorEQUALSEQUALS__String_String( ROGUE_ARG(THIS->value), Rogue_literal_strings[185] ))))) || ((RogueString__operatorEQUALSEQUALS__String_String( ROGUE_ARG(THIS->value), Rogue_literal_strings[186] )))));
}

RogueReal64 RogueStringValue__to_Real64( RogueClassStringValue* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueReal64)(strtod( (char*)THIS->value->utf8, 0 ));
}

RogueObject* RogueStringValue__to_Object( RogueClassStringValue* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueObject*)(((RogueObject*)(THIS->value)));
}

RogueStringBuilder* RogueStringValue__to_json__StringBuilder_Int32( RogueClassStringValue* THIS, RogueStringBuilder* buffer_0, RogueInt32 flags_1 )
{
  ROGUE_GC_CHECK;
  return (RogueStringBuilder*)((RogueStringValue__to_json__String_StringBuilder_Int32( ROGUE_ARG(THIS->value), buffer_0, flags_1 )));
}

RogueClassStringValue* RogueStringValue__init__String( RogueClassStringValue* THIS, RogueString* _auto_331_0 )
{
  ROGUE_GC_CHECK;
  THIS->value = _auto_331_0;
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
  return (RogueString*)(Rogue_literal_strings[225]);
}

RogueClassOutOfBoundsError* RogueOutOfBoundsError__init_object( RogueClassOutOfBoundsError* THIS )
{
  ROGUE_GC_CHECK;
  RogueError__init_object( ROGUE_ARG(((RogueClassError*)THIS)) );
  return (RogueClassOutOfBoundsError*)(THIS);
}

RogueString* RogueOutOfBoundsError__type_name( RogueClassOutOfBoundsError* THIS )
{
  return (RogueString*)(Rogue_literal_strings[220]);
}

RogueClassOutOfBoundsError* RogueOutOfBoundsError__init__String( RogueClassOutOfBoundsError* THIS, RogueString* _auto_444_0 )
{
  ROGUE_GC_CHECK;
  THIS->message = _auto_444_0;
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

RogueClassTableEntry_String_TypeInfo_* RogueTableEntry_String_TypeInfo___init_object( RogueClassTableEntry_String_TypeInfo_* THIS )
{
  ROGUE_GC_CHECK;
  RogueObject__init_object( ROGUE_ARG(((RogueObject*)THIS)) );
  return (RogueClassTableEntry_String_TypeInfo_*)(THIS);
}

RogueString* RogueTableEntry_String_TypeInfo___to_String( RogueClassTableEntry_String_TypeInfo_* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[8] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], ROGUE_ARG(THIS->key) ))) )))), Rogue_literal_strings[76] )))), ROGUE_ARG(((RogueString*)(RogueString__operatorPLUS__Object( ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], Rogue_literal_strings[0] ))), ROGUE_ARG(((RogueObject*)(THIS->value))) )))) )))), Rogue_literal_strings[11] )))) ))));
}

RogueString* RogueTableEntry_String_TypeInfo___type_name( RogueClassTableEntry_String_TypeInfo_* THIS )
{
  return (RogueString*)(Rogue_literal_strings[176]);
}

RogueClassRequirementError* RogueRequirementError__init_object( RogueClassRequirementError* THIS )
{
  ROGUE_GC_CHECK;
  RogueError__init_object( ROGUE_ARG(((RogueClassError*)THIS)) );
  return (RogueClassRequirementError*)(THIS);
}

RogueString* RogueRequirementError__type_name( RogueClassRequirementError* THIS )
{
  return (RogueString*)(Rogue_literal_strings[221]);
}

RogueClassRequirementError* RogueRequirementError__init__String( RogueClassRequirementError* THIS, RogueString* requirement_0 )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(!!(requirement_0->character_count)))
  {
    THIS->message = ((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[131] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], requirement_0 ))) )))), Rogue_literal_strings[2] )))) )));
  }
  else
  {
    THIS->message = Rogue_literal_strings[132];
  }
  return (RogueClassRequirementError*)(THIS);
}

RogueClassRequirementError* RogueRequirementError___throw( RogueClassRequirementError* THIS )
{
  ROGUE_GC_CHECK;
  throw THIS;

}

RogueClassListRewriter_Character_* RogueListRewriter_Character___init_object( RogueClassListRewriter_Character_* THIS )
{
  ROGUE_GC_CHECK;
  RogueObject__init_object( ROGUE_ARG(((RogueObject*)THIS)) );
  return (RogueClassListRewriter_Character_*)(THIS);
}

RogueString* RogueListRewriter_Character___type_name( RogueClassListRewriter_Character_* THIS )
{
  return (RogueString*)(Rogue_literal_strings[173]);
}

RogueClassListRewriter_Character_* RogueListRewriter_Character___init__Character_List( RogueClassListRewriter_Character_* THIS, RogueCharacter_List* _auto_968_0 )
{
  ROGUE_GC_CHECK;
  THIS->list = _auto_968_0;
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

RogueClassOptionalBoxed_Int32_* RogueOptionalBoxed_Int32___init_object( RogueClassOptionalBoxed_Int32_* THIS )
{
  ROGUE_GC_CHECK;
  RogueBoxed__init_object( ROGUE_ARG(((RogueClassBoxed*)THIS)) );
  return (RogueClassOptionalBoxed_Int32_*)(THIS);
}

RogueString* RogueOptionalBoxed_Int32___to_String( RogueClassOptionalBoxed_Int32_* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(((RogueString*)(RogueOptionalInt32__to_String( THIS->value ))));
}

RogueString* RogueOptionalBoxed_Int32___type_name( RogueClassOptionalBoxed_Int32_* THIS )
{
  return (RogueString*)(Rogue_literal_strings[235]);
}

RogueClassOptionalBoxed_Int32_* RogueOptionalBoxed_Int32___init__OptionalInt32( RogueClassOptionalBoxed_Int32_* THIS, RogueOptionalInt32 _auto_1128_0 )
{
  ROGUE_GC_CHECK;
  THIS->value = _auto_1128_0;
  return (RogueClassOptionalBoxed_Int32_*)(THIS);
}

RogueOptionalInt32 RogueOptionalOptionalBoxed_Int32___to_Int32( RogueClassOptionalBoxed_Int32_* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueOptionalInt32)(THIS->value);
}

RogueClassFunction_1182* RogueFunction_1182__init_object( RogueClassFunction_1182* THIS )
{
  ROGUE_GC_CHECK;
  Rogue_Function_____init_object( ROGUE_ARG(((RogueClass_Function___*)THIS)) );
  return (RogueClassFunction_1182*)(THIS);
}

RogueString* RogueFunction_1182__type_name( RogueClassFunction_1182* THIS )
{
  return (RogueString*)(Rogue_literal_strings[217]);
}

void RogueFunction_1182__call( RogueClassFunction_1182* THIS )
{
  RogueConsole__reset_input_mode( ROGUE_ARG(THIS->console) );
}

RogueClassFunction_1182* RogueFunction_1182__init__Console( RogueClassFunction_1182* THIS, RogueClassConsole* _auto_1183_0 )
{
  ROGUE_GC_CHECK;
  THIS->console = _auto_1183_0;
  return (RogueClassFunction_1182*)(THIS);
}

RogueClassBoxed_SystemEnvironment_* RogueBoxed_SystemEnvironment___init_object( RogueClassBoxed_SystemEnvironment_* THIS )
{
  ROGUE_GC_CHECK;
  RogueBoxed__init_object( ROGUE_ARG(((RogueClassBoxed*)THIS)) );
  return (RogueClassBoxed_SystemEnvironment_*)(THIS);
}

RogueString* RogueBoxed_SystemEnvironment___to_String( RogueClassBoxed_SystemEnvironment_* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(((RogueString*)(RogueSystemEnvironment__to_String( THIS->value ))));
}

RogueString* RogueBoxed_SystemEnvironment___type_name( RogueClassBoxed_SystemEnvironment_* THIS )
{
  return (RogueString*)(Rogue_literal_strings[236]);
}

RogueClassBoxed_SystemEnvironment_* RogueBoxed_SystemEnvironment___init__SystemEnvironment( RogueClassBoxed_SystemEnvironment_* THIS, RogueClassSystemEnvironment _auto_1209_0 )
{
  ROGUE_GC_CHECK;
  THIS->value = _auto_1209_0;
  return (RogueClassBoxed_SystemEnvironment_*)(THIS);
}

RogueClassSystemEnvironment RogueBoxed_SystemEnvironment___to_SystemEnvironment( RogueClassBoxed_SystemEnvironment_* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueClassSystemEnvironment)(THIS->value);
}

RogueClassIOError* RogueIOError__init_object( RogueClassIOError* THIS )
{
  ROGUE_GC_CHECK;
  RogueError__init_object( ROGUE_ARG(((RogueClassError*)THIS)) );
  return (RogueClassIOError*)(THIS);
}

RogueString* RogueIOError__type_name( RogueClassIOError* THIS )
{
  return (RogueString*)(Rogue_literal_strings[222]);
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
  return (RogueString*)(Rogue_literal_strings[174]);
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
    throw ((RogueClassIOError*)(RogueIOError___throw( ROGUE_ARG(((RogueClassIOError*)(((RogueException*)Rogue_call_ROGUEM3( 13, ROGUE_ARG(((RogueException*)ROGUE_CREATE_REF(RogueClassIOError*,ROGUE_CREATE_OBJECT(IOError)))), ROGUE_ARG(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__print__String( ROGUE_ARG(((RogueStringBuilder*)(RogueStringBuilder__init( ROGUE_ARG(ROGUE_CREATE_REF(RogueStringBuilder*,ROGUE_CREATE_OBJECT(StringBuilder))) )))), Rogue_literal_strings[24] )))), ROGUE_ARG((RogueString__operatorPLUS__String_String( Rogue_literal_strings[0], _filepath_0 ))) )))), Rogue_literal_strings[25] )))) )))) ))))) )));
  }
  return (RogueClassFileReader*)(THIS);
}

void RogueFileReader__on_cleanup( RogueClassFileReader* THIS )
{
  ROGUE_GC_CHECK;
  RogueFileReader__close( ROGUE_ARG(THIS) );
}

RogueLogical RogueFileReader__open__String( RogueClassFileReader* THIS, RogueString* _auto_1212_0 )
{
  ROGUE_GC_CHECK;
  THIS->filepath = _auto_1212_0;
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
  return (RogueString*)(Rogue_literal_strings[175]);
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

RogueClassUTF8Reader* RogueUTF8Reader__init__Reader_Byte_( RogueClassUTF8Reader* THIS, RogueClassReader_Byte_* _auto_1236_0 )
{
  ROGUE_GC_CHECK;
  THIS->byte_reader = _auto_1236_0;
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
  return (RogueString*)(Rogue_literal_strings[223]);
}

RogueClassJSONParseError* RogueJSONParseError__init__String( RogueClassJSONParseError* THIS, RogueString* _auto_1255_0 )
{
  ROGUE_GC_CHECK;
  THIS->message = _auto_1255_0;
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
  return (RogueString*)(Rogue_literal_strings[207]);
}

RogueClassOptionalBoxed_Byte_* RogueOptionalBoxed_Byte___init_object( RogueClassOptionalBoxed_Byte_* THIS )
{
  ROGUE_GC_CHECK;
  RogueBoxed__init_object( ROGUE_ARG(((RogueClassBoxed*)THIS)) );
  return (RogueClassOptionalBoxed_Byte_*)(THIS);
}

RogueString* RogueOptionalBoxed_Byte___to_String( RogueClassOptionalBoxed_Byte_* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(((RogueString*)(RogueOptionalByte__to_String( THIS->value ))));
}

RogueString* RogueOptionalBoxed_Byte___type_name( RogueClassOptionalBoxed_Byte_* THIS )
{
  return (RogueString*)(Rogue_literal_strings[237]);
}

RogueClassOptionalBoxed_Byte_* RogueOptionalBoxed_Byte___init__OptionalByte( RogueClassOptionalBoxed_Byte_* THIS, RogueOptionalByte _auto_1258_0 )
{
  ROGUE_GC_CHECK;
  THIS->value = _auto_1258_0;
  return (RogueClassOptionalBoxed_Byte_*)(THIS);
}

RogueOptionalByte RogueOptionalOptionalBoxed_Byte___to_Byte( RogueClassOptionalBoxed_Byte_* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueOptionalByte)(THIS->value);
}

RogueClassOptionalBoxed_Value_* RogueOptionalBoxed_Value___init_object( RogueClassOptionalBoxed_Value_* THIS )
{
  ROGUE_GC_CHECK;
  RogueBoxed__init_object( ROGUE_ARG(((RogueClassBoxed*)THIS)) );
  return (RogueClassOptionalBoxed_Value_*)(THIS);
}

RogueString* RogueOptionalBoxed_Value___to_String( RogueClassOptionalBoxed_Value_* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(((RogueString*)(RogueOptionalValue__to_String( THIS->value ))));
}

RogueString* RogueOptionalBoxed_Value___type_name( RogueClassOptionalBoxed_Value_* THIS )
{
  return (RogueString*)(Rogue_literal_strings[238]);
}

RogueClassOptionalBoxed_Value_* RogueOptionalBoxed_Value___init__OptionalValue( RogueClassOptionalBoxed_Value_* THIS, RogueOptionalValue _auto_1264_0 )
{
  ROGUE_GC_CHECK;
  THIS->value = _auto_1264_0;
  return (RogueClassOptionalBoxed_Value_*)(THIS);
}

RogueOptionalValue RogueOptionalOptionalBoxed_Value___to_Value( RogueClassOptionalBoxed_Value_* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueOptionalValue)(THIS->value);
}

RogueClassOptionalBoxed_StringBuilder_* RogueOptionalBoxed_StringBuilder___init_object( RogueClassOptionalBoxed_StringBuilder_* THIS )
{
  ROGUE_GC_CHECK;
  RogueBoxed__init_object( ROGUE_ARG(((RogueClassBoxed*)THIS)) );
  return (RogueClassOptionalBoxed_StringBuilder_*)(THIS);
}

RogueString* RogueOptionalBoxed_StringBuilder___to_String( RogueClassOptionalBoxed_StringBuilder_* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(((RogueString*)(RogueOptionalStringBuilder__to_String( THIS->value ))));
}

RogueString* RogueOptionalBoxed_StringBuilder___type_name( RogueClassOptionalBoxed_StringBuilder_* THIS )
{
  return (RogueString*)(Rogue_literal_strings[239]);
}

RogueClassOptionalBoxed_StringBuilder_* RogueOptionalBoxed_StringBuilder___init__OptionalStringBuilder( RogueClassOptionalBoxed_StringBuilder_* THIS, RogueOptionalStringBuilder _auto_1342_0 )
{
  ROGUE_GC_CHECK;
  THIS->value = _auto_1342_0;
  return (RogueClassOptionalBoxed_StringBuilder_*)(THIS);
}

RogueOptionalStringBuilder RogueOptionalOptionalBoxed_StringBuilder___to_StringBuilder( RogueClassOptionalBoxed_StringBuilder_* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueOptionalStringBuilder)(THIS->value);
}

RogueClassOptionalBoxed__Function____* RogueOptionalBoxed__Function______init_object( RogueClassOptionalBoxed__Function____* THIS )
{
  ROGUE_GC_CHECK;
  RogueBoxed__init_object( ROGUE_ARG(((RogueClassBoxed*)THIS)) );
  return (RogueClassOptionalBoxed__Function____*)(THIS);
}

RogueString* RogueOptionalBoxed__Function______to_String( RogueClassOptionalBoxed__Function____* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(((RogueString*)(RogueOptional_Function_____to_String( THIS->value ))));
}

RogueString* RogueOptionalBoxed__Function______type_name( RogueClassOptionalBoxed__Function____* THIS )
{
  return (RogueString*)(Rogue_literal_strings[240]);
}

RogueClassOptionalBoxed__Function____* RogueOptionalBoxed__Function______init__Optional_Function___( RogueClassOptionalBoxed__Function____* THIS, RogueOptional_Function___ _auto_1351_0 )
{
  ROGUE_GC_CHECK;
  THIS->value = _auto_1351_0;
  return (RogueClassOptionalBoxed__Function____*)(THIS);
}

RogueOptional_Function___ RogueOptionalOptionalBoxed__Function______to__Function___( RogueClassOptionalBoxed__Function____* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueOptional_Function___)(THIS->value);
}

RogueClassOptionalBoxed_String_* RogueOptionalBoxed_String___init_object( RogueClassOptionalBoxed_String_* THIS )
{
  ROGUE_GC_CHECK;
  RogueBoxed__init_object( ROGUE_ARG(((RogueClassBoxed*)THIS)) );
  return (RogueClassOptionalBoxed_String_*)(THIS);
}

RogueString* RogueOptionalBoxed_String___to_String( RogueClassOptionalBoxed_String_* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(((RogueString*)(RogueOptionalString__to_String( THIS->value ))));
}

RogueString* RogueOptionalBoxed_String___type_name( RogueClassOptionalBoxed_String_* THIS )
{
  return (RogueString*)(Rogue_literal_strings[241]);
}

RogueClassOptionalBoxed_String_* RogueOptionalBoxed_String___init__OptionalString( RogueClassOptionalBoxed_String_* THIS, RogueOptionalString _auto_1360_0 )
{
  ROGUE_GC_CHECK;
  THIS->value = _auto_1360_0;
  return (RogueClassOptionalBoxed_String_*)(THIS);
}

RogueOptionalString RogueOptionalOptionalBoxed_String___to_String( RogueClassOptionalBoxed_String_* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueOptionalString)(THIS->value);
}

RogueClassOptionalBoxed_PropertyInfo_* RogueOptionalBoxed_PropertyInfo___init_object( RogueClassOptionalBoxed_PropertyInfo_* THIS )
{
  ROGUE_GC_CHECK;
  RogueBoxed__init_object( ROGUE_ARG(((RogueClassBoxed*)THIS)) );
  return (RogueClassOptionalBoxed_PropertyInfo_*)(THIS);
}

RogueString* RogueOptionalBoxed_PropertyInfo___to_String( RogueClassOptionalBoxed_PropertyInfo_* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(((RogueString*)(RogueOptionalPropertyInfo__to_String( THIS->value ))));
}

RogueString* RogueOptionalBoxed_PropertyInfo___type_name( RogueClassOptionalBoxed_PropertyInfo_* THIS )
{
  return (RogueString*)(Rogue_literal_strings[242]);
}

RogueClassOptionalBoxed_PropertyInfo_* RogueOptionalBoxed_PropertyInfo___init__OptionalPropertyInfo( RogueClassOptionalBoxed_PropertyInfo_* THIS, RogueOptionalPropertyInfo _auto_1474_0 )
{
  ROGUE_GC_CHECK;
  THIS->value = _auto_1474_0;
  return (RogueClassOptionalBoxed_PropertyInfo_*)(THIS);
}

RogueOptionalPropertyInfo RogueOptionalOptionalBoxed_PropertyInfo___to_PropertyInfo( RogueClassOptionalBoxed_PropertyInfo_* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueOptionalPropertyInfo)(THIS->value);
}

RogueClassOptionalBoxed_MethodInfo_* RogueOptionalBoxed_MethodInfo___init_object( RogueClassOptionalBoxed_MethodInfo_* THIS )
{
  ROGUE_GC_CHECK;
  RogueBoxed__init_object( ROGUE_ARG(((RogueClassBoxed*)THIS)) );
  return (RogueClassOptionalBoxed_MethodInfo_*)(THIS);
}

RogueString* RogueOptionalBoxed_MethodInfo___to_String( RogueClassOptionalBoxed_MethodInfo_* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(((RogueString*)(RogueOptionalMethodInfo__to_String( THIS->value ))));
}

RogueString* RogueOptionalBoxed_MethodInfo___type_name( RogueClassOptionalBoxed_MethodInfo_* THIS )
{
  return (RogueString*)(Rogue_literal_strings[243]);
}

RogueClassOptionalBoxed_MethodInfo_* RogueOptionalBoxed_MethodInfo___init__OptionalMethodInfo( RogueClassOptionalBoxed_MethodInfo_* THIS, RogueOptionalMethodInfo _auto_1483_0 )
{
  ROGUE_GC_CHECK;
  THIS->value = _auto_1483_0;
  return (RogueClassOptionalBoxed_MethodInfo_*)(THIS);
}

RogueOptionalMethodInfo RogueOptionalOptionalBoxed_MethodInfo___to_MethodInfo( RogueClassOptionalBoxed_MethodInfo_* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueOptionalMethodInfo)(THIS->value);
}

RogueClassOptionalBoxed_Character_* RogueOptionalBoxed_Character___init_object( RogueClassOptionalBoxed_Character_* THIS )
{
  ROGUE_GC_CHECK;
  RogueBoxed__init_object( ROGUE_ARG(((RogueClassBoxed*)THIS)) );
  return (RogueClassOptionalBoxed_Character_*)(THIS);
}

RogueString* RogueOptionalBoxed_Character___to_String( RogueClassOptionalBoxed_Character_* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(((RogueString*)(RogueOptionalCharacter__to_String( THIS->value ))));
}

RogueString* RogueOptionalBoxed_Character___type_name( RogueClassOptionalBoxed_Character_* THIS )
{
  return (RogueString*)(Rogue_literal_strings[244]);
}

RogueClassOptionalBoxed_Character_* RogueOptionalBoxed_Character___init__OptionalCharacter( RogueClassOptionalBoxed_Character_* THIS, RogueOptionalCharacter _auto_1629_0 )
{
  ROGUE_GC_CHECK;
  THIS->value = _auto_1629_0;
  return (RogueClassOptionalBoxed_Character_*)(THIS);
}

RogueOptionalCharacter RogueOptionalOptionalBoxed_Character___to_Character( RogueClassOptionalBoxed_Character_* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueOptionalCharacter)(THIS->value);
}

RogueClassOptionalBoxed_Real64_* RogueOptionalBoxed_Real64___init_object( RogueClassOptionalBoxed_Real64_* THIS )
{
  ROGUE_GC_CHECK;
  RogueBoxed__init_object( ROGUE_ARG(((RogueClassBoxed*)THIS)) );
  return (RogueClassOptionalBoxed_Real64_*)(THIS);
}

RogueString* RogueOptionalBoxed_Real64___to_String( RogueClassOptionalBoxed_Real64_* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(((RogueString*)(RogueOptionalReal64__to_String( THIS->value ))));
}

RogueString* RogueOptionalBoxed_Real64___type_name( RogueClassOptionalBoxed_Real64_* THIS )
{
  return (RogueString*)(Rogue_literal_strings[245]);
}

RogueClassOptionalBoxed_Real64_* RogueOptionalBoxed_Real64___init__OptionalReal64( RogueClassOptionalBoxed_Real64_* THIS, RogueOptionalReal64 _auto_1647_0 )
{
  ROGUE_GC_CHECK;
  THIS->value = _auto_1647_0;
  return (RogueClassOptionalBoxed_Real64_*)(THIS);
}

RogueOptionalReal64 RogueOptionalOptionalBoxed_Real64___to_Real64( RogueClassOptionalBoxed_Real64_* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueOptionalReal64)(THIS->value);
}

RogueClassOptionalBoxed_Real32_* RogueOptionalBoxed_Real32___init_object( RogueClassOptionalBoxed_Real32_* THIS )
{
  ROGUE_GC_CHECK;
  RogueBoxed__init_object( ROGUE_ARG(((RogueClassBoxed*)THIS)) );
  return (RogueClassOptionalBoxed_Real32_*)(THIS);
}

RogueString* RogueOptionalBoxed_Real32___to_String( RogueClassOptionalBoxed_Real32_* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(((RogueString*)(RogueOptionalReal32__to_String( THIS->value ))));
}

RogueString* RogueOptionalBoxed_Real32___type_name( RogueClassOptionalBoxed_Real32_* THIS )
{
  return (RogueString*)(Rogue_literal_strings[246]);
}

RogueClassOptionalBoxed_Real32_* RogueOptionalBoxed_Real32___init__OptionalReal32( RogueClassOptionalBoxed_Real32_* THIS, RogueOptionalReal32 _auto_1650_0 )
{
  ROGUE_GC_CHECK;
  THIS->value = _auto_1650_0;
  return (RogueClassOptionalBoxed_Real32_*)(THIS);
}

RogueOptionalReal32 RogueOptionalOptionalBoxed_Real32___to_Real32( RogueClassOptionalBoxed_Real32_* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueOptionalReal32)(THIS->value);
}

RogueClassOptionalBoxed_Int64_* RogueOptionalBoxed_Int64___init_object( RogueClassOptionalBoxed_Int64_* THIS )
{
  ROGUE_GC_CHECK;
  RogueBoxed__init_object( ROGUE_ARG(((RogueClassBoxed*)THIS)) );
  return (RogueClassOptionalBoxed_Int64_*)(THIS);
}

RogueString* RogueOptionalBoxed_Int64___to_String( RogueClassOptionalBoxed_Int64_* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(((RogueString*)(RogueOptionalInt64__to_String( THIS->value ))));
}

RogueString* RogueOptionalBoxed_Int64___type_name( RogueClassOptionalBoxed_Int64_* THIS )
{
  return (RogueString*)(Rogue_literal_strings[247]);
}

RogueClassOptionalBoxed_Int64_* RogueOptionalBoxed_Int64___init__OptionalInt64( RogueClassOptionalBoxed_Int64_* THIS, RogueOptionalInt64 _auto_1653_0 )
{
  ROGUE_GC_CHECK;
  THIS->value = _auto_1653_0;
  return (RogueClassOptionalBoxed_Int64_*)(THIS);
}

RogueOptionalInt64 RogueOptionalOptionalBoxed_Int64___to_Int64( RogueClassOptionalBoxed_Int64_* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueOptionalInt64)(THIS->value);
}

RogueClassBoxed_FileOptions_* RogueBoxed_FileOptions___init_object( RogueClassBoxed_FileOptions_* THIS )
{
  ROGUE_GC_CHECK;
  RogueBoxed__init_object( ROGUE_ARG(((RogueClassBoxed*)THIS)) );
  return (RogueClassBoxed_FileOptions_*)(THIS);
}

RogueString* RogueBoxed_FileOptions___to_String( RogueClassBoxed_FileOptions_* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(((RogueString*)(RogueFileOptions__to_String( THIS->value ))));
}

RogueString* RogueBoxed_FileOptions___type_name( RogueClassBoxed_FileOptions_* THIS )
{
  return (RogueString*)(Rogue_literal_strings[248]);
}

RogueClassBoxed_FileOptions_* RogueBoxed_FileOptions___init__FileOptions( RogueClassBoxed_FileOptions_* THIS, RogueClassFileOptions _auto_1728_0 )
{
  ROGUE_GC_CHECK;
  THIS->value = _auto_1728_0;
  return (RogueClassBoxed_FileOptions_*)(THIS);
}

RogueClassFileOptions RogueBoxed_FileOptions___to_FileOptions( RogueClassBoxed_FileOptions_* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueClassFileOptions)(THIS->value);
}

RogueClassOptionalBoxed_TableEntry_String_Value__* RogueOptionalBoxed_TableEntry_String_Value____init_object( RogueClassOptionalBoxed_TableEntry_String_Value__* THIS )
{
  ROGUE_GC_CHECK;
  RogueBoxed__init_object( ROGUE_ARG(((RogueClassBoxed*)THIS)) );
  return (RogueClassOptionalBoxed_TableEntry_String_Value__*)(THIS);
}

RogueString* RogueOptionalBoxed_TableEntry_String_Value____to_String( RogueClassOptionalBoxed_TableEntry_String_Value__* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(((RogueString*)(RogueOptionalTableEntry_String_Value___to_String( THIS->value ))));
}

RogueString* RogueOptionalBoxed_TableEntry_String_Value____type_name( RogueClassOptionalBoxed_TableEntry_String_Value__* THIS )
{
  return (RogueString*)(Rogue_literal_strings[249]);
}

RogueClassOptionalBoxed_TableEntry_String_Value__* RogueOptionalBoxed_TableEntry_String_Value____init__OptionalTableEntry_String_Value_( RogueClassOptionalBoxed_TableEntry_String_Value__* THIS, RogueOptionalTableEntry_String_Value_ _auto_1740_0 )
{
  ROGUE_GC_CHECK;
  THIS->value = _auto_1740_0;
  return (RogueClassOptionalBoxed_TableEntry_String_Value__*)(THIS);
}

RogueOptionalTableEntry_String_Value_ RogueOptionalOptionalBoxed_TableEntry_String_Value____to_TableEntry_String_Value_( RogueClassOptionalBoxed_TableEntry_String_Value__* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueOptionalTableEntry_String_Value_)(THIS->value);
}

RogueClassOptionalBoxed_TypeInfo_* RogueOptionalBoxed_TypeInfo___init_object( RogueClassOptionalBoxed_TypeInfo_* THIS )
{
  ROGUE_GC_CHECK;
  RogueBoxed__init_object( ROGUE_ARG(((RogueClassBoxed*)THIS)) );
  return (RogueClassOptionalBoxed_TypeInfo_*)(THIS);
}

RogueString* RogueOptionalBoxed_TypeInfo___to_String( RogueClassOptionalBoxed_TypeInfo_* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(((RogueString*)(RogueOptionalTypeInfo__to_String( THIS->value ))));
}

RogueString* RogueOptionalBoxed_TypeInfo___type_name( RogueClassOptionalBoxed_TypeInfo_* THIS )
{
  return (RogueString*)(Rogue_literal_strings[250]);
}

RogueClassOptionalBoxed_TypeInfo_* RogueOptionalBoxed_TypeInfo___init__OptionalTypeInfo( RogueClassOptionalBoxed_TypeInfo_* THIS, RogueOptionalTypeInfo _auto_1758_0 )
{
  ROGUE_GC_CHECK;
  THIS->value = _auto_1758_0;
  return (RogueClassOptionalBoxed_TypeInfo_*)(THIS);
}

RogueOptionalTypeInfo RogueOptionalOptionalBoxed_TypeInfo___to_TypeInfo( RogueClassOptionalBoxed_TypeInfo_* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueOptionalTypeInfo)(THIS->value);
}

RogueClassOptionalBoxed_TableEntry_Int32_String__* RogueOptionalBoxed_TableEntry_Int32_String____init_object( RogueClassOptionalBoxed_TableEntry_Int32_String__* THIS )
{
  ROGUE_GC_CHECK;
  RogueBoxed__init_object( ROGUE_ARG(((RogueClassBoxed*)THIS)) );
  return (RogueClassOptionalBoxed_TableEntry_Int32_String__*)(THIS);
}

RogueString* RogueOptionalBoxed_TableEntry_Int32_String____to_String( RogueClassOptionalBoxed_TableEntry_Int32_String__* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(((RogueString*)(RogueOptionalTableEntry_Int32_String___to_String( THIS->value ))));
}

RogueString* RogueOptionalBoxed_TableEntry_Int32_String____type_name( RogueClassOptionalBoxed_TableEntry_Int32_String__* THIS )
{
  return (RogueString*)(Rogue_literal_strings[251]);
}

RogueClassOptionalBoxed_TableEntry_Int32_String__* RogueOptionalBoxed_TableEntry_Int32_String____init__OptionalTableEntry_Int32_String_( RogueClassOptionalBoxed_TableEntry_Int32_String__* THIS, RogueOptionalTableEntry_Int32_String_ _auto_1848_0 )
{
  ROGUE_GC_CHECK;
  THIS->value = _auto_1848_0;
  return (RogueClassOptionalBoxed_TableEntry_Int32_String__*)(THIS);
}

RogueOptionalTableEntry_Int32_String_ RogueOptionalOptionalBoxed_TableEntry_Int32_String____to_TableEntry_Int32_String_( RogueClassOptionalBoxed_TableEntry_Int32_String__* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueOptionalTableEntry_Int32_String_)(THIS->value);
}

RogueClassOptionalBoxed_TableEntry_String_String__* RogueOptionalBoxed_TableEntry_String_String____init_object( RogueClassOptionalBoxed_TableEntry_String_String__* THIS )
{
  ROGUE_GC_CHECK;
  RogueBoxed__init_object( ROGUE_ARG(((RogueClassBoxed*)THIS)) );
  return (RogueClassOptionalBoxed_TableEntry_String_String__*)(THIS);
}

RogueString* RogueOptionalBoxed_TableEntry_String_String____to_String( RogueClassOptionalBoxed_TableEntry_String_String__* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(((RogueString*)(RogueOptionalTableEntry_String_String___to_String( THIS->value ))));
}

RogueString* RogueOptionalBoxed_TableEntry_String_String____type_name( RogueClassOptionalBoxed_TableEntry_String_String__* THIS )
{
  return (RogueString*)(Rogue_literal_strings[252]);
}

RogueClassOptionalBoxed_TableEntry_String_String__* RogueOptionalBoxed_TableEntry_String_String____init__OptionalTableEntry_String_String_( RogueClassOptionalBoxed_TableEntry_String_String__* THIS, RogueOptionalTableEntry_String_String_ _auto_1869_0 )
{
  ROGUE_GC_CHECK;
  THIS->value = _auto_1869_0;
  return (RogueClassOptionalBoxed_TableEntry_String_String__*)(THIS);
}

RogueOptionalTableEntry_String_String_ RogueOptionalOptionalBoxed_TableEntry_String_String____to_TableEntry_String_String_( RogueClassOptionalBoxed_TableEntry_String_String__* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueOptionalTableEntry_String_String_)(THIS->value);
}

RogueClassOptionalBoxed_TableEntry_String_TypeInfo__* RogueOptionalBoxed_TableEntry_String_TypeInfo____init_object( RogueClassOptionalBoxed_TableEntry_String_TypeInfo__* THIS )
{
  ROGUE_GC_CHECK;
  RogueBoxed__init_object( ROGUE_ARG(((RogueClassBoxed*)THIS)) );
  return (RogueClassOptionalBoxed_TableEntry_String_TypeInfo__*)(THIS);
}

RogueString* RogueOptionalBoxed_TableEntry_String_TypeInfo____to_String( RogueClassOptionalBoxed_TableEntry_String_TypeInfo__* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueString*)(((RogueString*)(RogueOptionalTableEntry_String_TypeInfo___to_String( THIS->value ))));
}

RogueString* RogueOptionalBoxed_TableEntry_String_TypeInfo____type_name( RogueClassOptionalBoxed_TableEntry_String_TypeInfo__* THIS )
{
  return (RogueString*)(Rogue_literal_strings[253]);
}

RogueClassOptionalBoxed_TableEntry_String_TypeInfo__* RogueOptionalBoxed_TableEntry_String_TypeInfo____init__OptionalTableEntry_String_TypeInfo_( RogueClassOptionalBoxed_TableEntry_String_TypeInfo__* THIS, RogueOptionalTableEntry_String_TypeInfo_ _auto_1887_0 )
{
  ROGUE_GC_CHECK;
  THIS->value = _auto_1887_0;
  return (RogueClassOptionalBoxed_TableEntry_String_TypeInfo__*)(THIS);
}

RogueOptionalTableEntry_String_TypeInfo_ RogueOptionalOptionalBoxed_TableEntry_String_TypeInfo____to_TableEntry_String_TypeInfo_( RogueClassOptionalBoxed_TableEntry_String_TypeInfo__* THIS )
{
  ROGUE_GC_CHECK;
  return (RogueOptionalTableEntry_String_TypeInfo_)(THIS->value);
}

RogueClassValue* RogueOptionalInt32__to_Value( RogueOptionalInt32 THIS )
{
  ROGUE_GC_CHECK;
  return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueClassOptionalBoxed_Int32_*)(RogueOptionalBoxed_Int32___init__OptionalInt32( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassOptionalBoxed_Int32_*,ROGUE_CREATE_OBJECT(OptionalBoxed_Int32_))), THIS )))))) )));
}

RogueString* RogueOptionalInt32__to_String( RogueOptionalInt32 THIS )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(THIS.exists))
  {
    return (RogueString*)(((RogueString*)(RogueInt32__to_String( ROGUE_ARG(THIS.value) ))));
  }
  else
  {
    return (RogueString*)(Rogue_literal_strings[1]);
  }
}

RogueClassValue* RogueSystemEnvironment__to_Value( RogueClassSystemEnvironment THIS )
{
  ROGUE_GC_CHECK;
  return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueClassBoxed_SystemEnvironment_*)(RogueBoxed_SystemEnvironment___init__SystemEnvironment( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassBoxed_SystemEnvironment_*,ROGUE_CREATE_OBJECT(Boxed_SystemEnvironment_))), THIS )))))) )));
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

RogueString* RogueSystemEnvironment__to_String( RogueClassSystemEnvironment THIS )
{
  return (RogueString*)(Rogue_literal_strings[229]);
}

RogueClassValue* RogueOptionalByte__to_Value( RogueOptionalByte THIS )
{
  ROGUE_GC_CHECK;
  return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueClassOptionalBoxed_Byte_*)(RogueOptionalBoxed_Byte___init__OptionalByte( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassOptionalBoxed_Byte_*,ROGUE_CREATE_OBJECT(OptionalBoxed_Byte_))), THIS )))))) )));
}

RogueString* RogueOptionalByte__to_String( RogueOptionalByte THIS )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(THIS.exists))
  {
    return (RogueString*)(((RogueString*)(RogueByte__to_String( ROGUE_ARG(THIS.value) ))));
  }
  else
  {
    return (RogueString*)(Rogue_literal_strings[1]);
  }
}

RogueClassValue* RogueOptionalValue__to_Value( RogueOptionalValue THIS )
{
  ROGUE_GC_CHECK;
  return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueClassOptionalBoxed_Value_*)(RogueOptionalBoxed_Value___init__OptionalValue( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassOptionalBoxed_Value_*,ROGUE_CREATE_OBJECT(OptionalBoxed_Value_))), THIS )))))) )));
}

RogueString* RogueOptionalValue__to_String( RogueOptionalValue THIS )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(THIS.exists))
  {
    return (RogueString*)(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)THIS.value)) )));
  }
  else
  {
    return (RogueString*)(Rogue_literal_strings[1]);
  }
}

RogueClassValue* RogueOptionalStringBuilder__to_Value( RogueOptionalStringBuilder THIS )
{
  ROGUE_GC_CHECK;
  return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueClassOptionalBoxed_StringBuilder_*)(RogueOptionalBoxed_StringBuilder___init__OptionalStringBuilder( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassOptionalBoxed_StringBuilder_*,ROGUE_CREATE_OBJECT(OptionalBoxed_StringBuilder_))), THIS )))))) )));
}

RogueString* RogueOptionalStringBuilder__to_String( RogueOptionalStringBuilder THIS )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(THIS.exists))
  {
    return (RogueString*)(((RogueString*)(RogueStringBuilder__to_String( ROGUE_ARG(THIS.value) ))));
  }
  else
  {
    return (RogueString*)(Rogue_literal_strings[1]);
  }
}

RogueClassValue* RogueOptional_Function_____to_Value( RogueOptional_Function___ THIS )
{
  ROGUE_GC_CHECK;
  return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueClassOptionalBoxed__Function____*)(RogueOptionalBoxed__Function______init__Optional_Function___( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassOptionalBoxed__Function____*,ROGUE_CREATE_OBJECT(OptionalBoxed__Function____))), THIS )))))) )));
}

RogueString* RogueOptional_Function_____to_String( RogueOptional_Function___ THIS )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(THIS.exists))
  {
    return (RogueString*)(((RogueString*)Rogue_call_ROGUEM1( 6, ROGUE_ARG(((RogueObject*)THIS.value)) )));
  }
  else
  {
    return (RogueString*)(Rogue_literal_strings[1]);
  }
}

RogueClassValue* RogueOptionalString__to_Value( RogueOptionalString THIS )
{
  ROGUE_GC_CHECK;
  return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueClassOptionalBoxed_String_*)(RogueOptionalBoxed_String___init__OptionalString( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassOptionalBoxed_String_*,ROGUE_CREATE_OBJECT(OptionalBoxed_String_))), THIS )))))) )));
}

RogueString* RogueOptionalString__to_String( RogueOptionalString THIS )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(THIS.exists))
  {
    return (RogueString*)(THIS.value);
  }
  else
  {
    return (RogueString*)(Rogue_literal_strings[1]);
  }
}

RogueClassValue* RogueOptionalPropertyInfo__to_Value( RogueOptionalPropertyInfo THIS )
{
  ROGUE_GC_CHECK;
  return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueClassOptionalBoxed_PropertyInfo_*)(RogueOptionalBoxed_PropertyInfo___init__OptionalPropertyInfo( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassOptionalBoxed_PropertyInfo_*,ROGUE_CREATE_OBJECT(OptionalBoxed_PropertyInfo_))), THIS )))))) )));
}

RogueString* RogueOptionalPropertyInfo__to_String( RogueOptionalPropertyInfo THIS )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(THIS.exists))
  {
    return (RogueString*)(((RogueString*)(RoguePropertyInfo__to_String( ROGUE_ARG(THIS.value) ))));
  }
  else
  {
    return (RogueString*)(Rogue_literal_strings[1]);
  }
}

RogueClassValue* RogueOptionalMethodInfo__to_Value( RogueOptionalMethodInfo THIS )
{
  ROGUE_GC_CHECK;
  return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueClassOptionalBoxed_MethodInfo_*)(RogueOptionalBoxed_MethodInfo___init__OptionalMethodInfo( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassOptionalBoxed_MethodInfo_*,ROGUE_CREATE_OBJECT(OptionalBoxed_MethodInfo_))), THIS )))))) )));
}

RogueString* RogueOptionalMethodInfo__to_String( RogueOptionalMethodInfo THIS )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(THIS.exists))
  {
    return (RogueString*)(((RogueString*)(RogueMethodInfo__to_String( ROGUE_ARG(THIS.value) ))));
  }
  else
  {
    return (RogueString*)(Rogue_literal_strings[1]);
  }
}

RogueClassValue* RogueOptionalCharacter__to_Value( RogueOptionalCharacter THIS )
{
  ROGUE_GC_CHECK;
  return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueClassOptionalBoxed_Character_*)(RogueOptionalBoxed_Character___init__OptionalCharacter( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassOptionalBoxed_Character_*,ROGUE_CREATE_OBJECT(OptionalBoxed_Character_))), THIS )))))) )));
}

RogueString* RogueOptionalCharacter__to_String( RogueOptionalCharacter THIS )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(THIS.exists))
  {
    return (RogueString*)(((RogueString*)(RogueCharacter__to_String( ROGUE_ARG(THIS.value) ))));
  }
  else
  {
    return (RogueString*)(Rogue_literal_strings[1]);
  }
}

RogueClassValue* RogueOptionalReal64__to_Value( RogueOptionalReal64 THIS )
{
  ROGUE_GC_CHECK;
  return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueClassOptionalBoxed_Real64_*)(RogueOptionalBoxed_Real64___init__OptionalReal64( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassOptionalBoxed_Real64_*,ROGUE_CREATE_OBJECT(OptionalBoxed_Real64_))), THIS )))))) )));
}

RogueString* RogueOptionalReal64__to_String( RogueOptionalReal64 THIS )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(THIS.exists))
  {
    return (RogueString*)(((RogueString*)(RogueReal64__to_String( ROGUE_ARG(THIS.value) ))));
  }
  else
  {
    return (RogueString*)(Rogue_literal_strings[1]);
  }
}

RogueClassValue* RogueOptionalReal32__to_Value( RogueOptionalReal32 THIS )
{
  ROGUE_GC_CHECK;
  return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueClassOptionalBoxed_Real32_*)(RogueOptionalBoxed_Real32___init__OptionalReal32( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassOptionalBoxed_Real32_*,ROGUE_CREATE_OBJECT(OptionalBoxed_Real32_))), THIS )))))) )));
}

RogueString* RogueOptionalReal32__to_String( RogueOptionalReal32 THIS )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(THIS.exists))
  {
    return (RogueString*)(((RogueString*)(RogueReal32__to_String( ROGUE_ARG(THIS.value) ))));
  }
  else
  {
    return (RogueString*)(Rogue_literal_strings[1]);
  }
}

RogueClassValue* RogueOptionalInt64__to_Value( RogueOptionalInt64 THIS )
{
  ROGUE_GC_CHECK;
  return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueClassOptionalBoxed_Int64_*)(RogueOptionalBoxed_Int64___init__OptionalInt64( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassOptionalBoxed_Int64_*,ROGUE_CREATE_OBJECT(OptionalBoxed_Int64_))), THIS )))))) )));
}

RogueString* RogueOptionalInt64__to_String( RogueOptionalInt64 THIS )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(THIS.exists))
  {
    return (RogueString*)(((RogueString*)(RogueInt64__to_String( ROGUE_ARG(THIS.value) ))));
  }
  else
  {
    return (RogueString*)(Rogue_literal_strings[1]);
  }
}

RogueClassValue* RogueFileOptions__to_Value( RogueClassFileOptions THIS )
{
  ROGUE_GC_CHECK;
  return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueClassBoxed_FileOptions_*)(RogueBoxed_FileOptions___init__FileOptions( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassBoxed_FileOptions_*,ROGUE_CREATE_OBJECT(Boxed_FileOptions_))), THIS )))))) )));
}

RogueString* RogueFileOptions__to_String( RogueClassFileOptions THIS )
{
  return (RogueString*)(Rogue_literal_strings[230]);
}

RogueClassValue* RogueOptionalTableEntry_String_Value___to_Value( RogueOptionalTableEntry_String_Value_ THIS )
{
  ROGUE_GC_CHECK;
  return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueClassOptionalBoxed_TableEntry_String_Value__*)(RogueOptionalBoxed_TableEntry_String_Value____init__OptionalTableEntry_String_Value_( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassOptionalBoxed_TableEntry_String_Value__*,ROGUE_CREATE_OBJECT(OptionalBoxed_TableEntry_String_Value__))), THIS )))))) )));
}

RogueString* RogueOptionalTableEntry_String_Value___to_String( RogueOptionalTableEntry_String_Value_ THIS )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(THIS.exists))
  {
    return (RogueString*)(((RogueString*)(RogueTableEntry_String_Value___to_String( ROGUE_ARG(THIS.value) ))));
  }
  else
  {
    return (RogueString*)(Rogue_literal_strings[1]);
  }
}

RogueClassValue* RogueOptionalTypeInfo__to_Value( RogueOptionalTypeInfo THIS )
{
  ROGUE_GC_CHECK;
  return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueClassOptionalBoxed_TypeInfo_*)(RogueOptionalBoxed_TypeInfo___init__OptionalTypeInfo( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassOptionalBoxed_TypeInfo_*,ROGUE_CREATE_OBJECT(OptionalBoxed_TypeInfo_))), THIS )))))) )));
}

RogueString* RogueOptionalTypeInfo__to_String( RogueOptionalTypeInfo THIS )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(THIS.exists))
  {
    return (RogueString*)(((RogueString*)(RogueTypeInfo__to_String( ROGUE_ARG(THIS.value) ))));
  }
  else
  {
    return (RogueString*)(Rogue_literal_strings[1]);
  }
}

RogueClassValue* RogueOptionalTableEntry_Int32_String___to_Value( RogueOptionalTableEntry_Int32_String_ THIS )
{
  ROGUE_GC_CHECK;
  return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueClassOptionalBoxed_TableEntry_Int32_String__*)(RogueOptionalBoxed_TableEntry_Int32_String____init__OptionalTableEntry_Int32_String_( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassOptionalBoxed_TableEntry_Int32_String__*,ROGUE_CREATE_OBJECT(OptionalBoxed_TableEntry_Int32_String__))), THIS )))))) )));
}

RogueString* RogueOptionalTableEntry_Int32_String___to_String( RogueOptionalTableEntry_Int32_String_ THIS )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(THIS.exists))
  {
    return (RogueString*)(((RogueString*)(RogueTableEntry_Int32_String___to_String( ROGUE_ARG(THIS.value) ))));
  }
  else
  {
    return (RogueString*)(Rogue_literal_strings[1]);
  }
}

RogueClassValue* RogueOptionalTableEntry_String_String___to_Value( RogueOptionalTableEntry_String_String_ THIS )
{
  ROGUE_GC_CHECK;
  return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueClassOptionalBoxed_TableEntry_String_String__*)(RogueOptionalBoxed_TableEntry_String_String____init__OptionalTableEntry_String_String_( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassOptionalBoxed_TableEntry_String_String__*,ROGUE_CREATE_OBJECT(OptionalBoxed_TableEntry_String_String__))), THIS )))))) )));
}

RogueString* RogueOptionalTableEntry_String_String___to_String( RogueOptionalTableEntry_String_String_ THIS )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(THIS.exists))
  {
    return (RogueString*)(((RogueString*)(RogueTableEntry_String_String___to_String( ROGUE_ARG(THIS.value) ))));
  }
  else
  {
    return (RogueString*)(Rogue_literal_strings[1]);
  }
}

RogueClassValue* RogueOptionalTableEntry_String_TypeInfo___to_Value( RogueOptionalTableEntry_String_TypeInfo_ THIS )
{
  ROGUE_GC_CHECK;
  return (RogueClassValue*)((RogueValue__create__Object( ROGUE_ARG(((RogueObject*)(((RogueClassOptionalBoxed_TableEntry_String_TypeInfo__*)(RogueOptionalBoxed_TableEntry_String_TypeInfo____init__OptionalTableEntry_String_TypeInfo_( ROGUE_ARG(ROGUE_CREATE_REF(RogueClassOptionalBoxed_TableEntry_String_TypeInfo__*,ROGUE_CREATE_OBJECT(OptionalBoxed_TableEntry_String_TypeInfo__))), THIS )))))) )));
}

RogueString* RogueOptionalTableEntry_String_TypeInfo___to_String( RogueOptionalTableEntry_String_TypeInfo_ THIS )
{
  ROGUE_GC_CHECK;
  if (ROGUE_COND(THIS.exists))
  {
    return (RogueString*)(((RogueString*)(RogueTableEntry_String_TypeInfo___to_String( ROGUE_ARG(THIS.value) ))));
  }
  else
  {
    return (RogueString*)(Rogue_literal_strings[1]);
  }
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
  RogueTypeReal32 = &Rogue_types[ 30 ];
  RogueTypeInt64 = &Rogue_types[ 31 ];
  RogueTypeCharacter = &Rogue_types[ 32 ];
  RogueTypeTypeInfo = &Rogue_types[ 33 ];
  RogueTypePropertyInfo_List = &Rogue_types[ 34 ];
  RogueTypePropertyInfo = &Rogue_types[ 35 ];
  RogueTypeMethodInfo_List = &Rogue_types[ 37 ];
  RogueTypeMethodInfo = &Rogue_types[ 38 ];
  RogueTypeTable_Int32_String_ = &Rogue_types[ 39 ];
  RogueTypeTableEntry_Int32_String_ = &Rogue_types[ 41 ];
  RogueType_Function_TableEntry_Int32_String__TableEntry_Int32_String__RETURNSLogical_ = &Rogue_types[ 42 ];
  RogueTypeCharacter_List = &Rogue_types[ 44 ];
  RogueTypeValueList = &Rogue_types[ 46 ];
  RogueTypeValue_List = &Rogue_types[ 47 ];
  RogueTypeObjectValue = &Rogue_types[ 49 ];
  RogueTypeStringConsolidationTable = &Rogue_types[ 50 ];
  RogueTypeStringTable_String_ = &Rogue_types[ 51 ];
  RogueTypeTable_String_String_ = &Rogue_types[ 52 ];
  RogueTypeTableEntry_String_String_ = &Rogue_types[ 54 ];
  RogueType_Function_TableEntry_String_String__TableEntry_String_String__RETURNSLogical_ = &Rogue_types[ 55 ];
  RogueTypeReader_Character_ = &Rogue_types[ 56 ];
  RogueTypeReader_String_ = &Rogue_types[ 57 ];
  RogueTypeConsole = &Rogue_types[ 58 ];
  RogueTypePrintWriter_output_buffer_ = &Rogue_types[ 59 ];
  RogueTypeConsoleErrorPrinter = &Rogue_types[ 60 ];
  RogueTypePrimitiveWorkBuffer = &Rogue_types[ 61 ];
  RogueTypeMath = &Rogue_types[ 62 ];
  RogueTypeBoxed = &Rogue_types[ 63 ];
  RogueTypeFunction_221 = &Rogue_types[ 64 ];
  RogueTypeSystem = &Rogue_types[ 65 ];
  RogueTypeError = &Rogue_types[ 66 ];
  RogueTypeFile = &Rogue_types[ 67 ];
  RogueTypeLineReader = &Rogue_types[ 68 ];
  RogueTypeReader_Byte_ = &Rogue_types[ 69 ];
  RogueTypeFileWriter = &Rogue_types[ 70 ];
  RogueTypeWriter_Byte_ = &Rogue_types[ 71 ];
  RogueTypeScanner = &Rogue_types[ 72 ];
  RogueTypeJSONParser = &Rogue_types[ 73 ];
  RogueTypeJSON = &Rogue_types[ 74 ];
  RogueType_Function_String_RETURNSString_ = &Rogue_types[ 75 ];
  RogueTypeFunction_281 = &Rogue_types[ 76 ];
  RogueTypeRuntime = &Rogue_types[ 77 ];
  RogueTypeWeakReference = &Rogue_types[ 78 ];
  RogueTypePrintWriterAdapter = &Rogue_types[ 79 ];
  RogueTypePrintWriter_buffer_ = &Rogue_types[ 80 ];
  RogueTypeLogicalValue = &Rogue_types[ 81 ];
  RogueTypeInt32Value = &Rogue_types[ 82 ];
  RogueTypeInt64Value = &Rogue_types[ 83 ];
  RogueTypeReal64Value = &Rogue_types[ 84 ];
  RogueTypeNullValue = &Rogue_types[ 85 ];
  RogueTypeStringValue = &Rogue_types[ 86 ];
  RogueTypeUndefinedValue = &Rogue_types[ 87 ];
  RogueTypeOutOfBoundsError = &Rogue_types[ 88 ];
  RogueTypeTableEntry_String_TypeInfo_ = &Rogue_types[ 89 ];
  RogueTypeRequirementError = &Rogue_types[ 90 ];
  RogueTypeListRewriter_Character_ = &Rogue_types[ 91 ];
  RogueTypeOptionalBoxed_Int32_ = &Rogue_types[ 92 ];
  RogueTypeFunction_1182 = &Rogue_types[ 93 ];
  RogueTypeBoxed_SystemEnvironment_ = &Rogue_types[ 94 ];
  RogueTypeIOError = &Rogue_types[ 95 ];
  RogueTypeFileReader = &Rogue_types[ 96 ];
  RogueTypeUTF8Reader = &Rogue_types[ 97 ];
  RogueTypeJSONParseError = &Rogue_types[ 98 ];
  RogueTypeJSONParserBuffer = &Rogue_types[ 99 ];
  RogueTypeOptionalBoxed_Byte_ = &Rogue_types[ 100 ];
  RogueTypeOptionalBoxed_Value_ = &Rogue_types[ 101 ];
  RogueTypeOptionalBoxed_StringBuilder_ = &Rogue_types[ 102 ];
  RogueTypeOptionalBoxed__Function____ = &Rogue_types[ 103 ];
  RogueTypeOptionalBoxed_String_ = &Rogue_types[ 104 ];
  RogueTypeOptionalBoxed_PropertyInfo_ = &Rogue_types[ 105 ];
  RogueTypeOptionalBoxed_MethodInfo_ = &Rogue_types[ 106 ];
  RogueTypeOptionalBoxed_Character_ = &Rogue_types[ 107 ];
  RogueTypeOptionalBoxed_Real64_ = &Rogue_types[ 108 ];
  RogueTypeOptionalBoxed_Real32_ = &Rogue_types[ 109 ];
  RogueTypeOptionalBoxed_Int64_ = &Rogue_types[ 110 ];
  RogueTypeBoxed_FileOptions_ = &Rogue_types[ 111 ];
  RogueTypeOptionalBoxed_TableEntry_String_Value__ = &Rogue_types[ 112 ];
  RogueTypeOptionalBoxed_TypeInfo_ = &Rogue_types[ 113 ];
  RogueTypeOptionalBoxed_TableEntry_Int32_String__ = &Rogue_types[ 114 ];
  RogueTypeOptionalBoxed_TableEntry_String_String__ = &Rogue_types[ 115 ];
  RogueTypeOptionalBoxed_TableEntry_String_TypeInfo__ = &Rogue_types[ 116 ];
  RogueTypeOptionalInt32 = &Rogue_types[ 117 ];
  RogueTypeSystemEnvironment = &Rogue_types[ 118 ];
  RogueTypeOptionalByte = &Rogue_types[ 119 ];
  RogueTypeOptionalValue = &Rogue_types[ 120 ];
  RogueTypeOptionalStringBuilder = &Rogue_types[ 121 ];
  RogueTypeOptional_Function___ = &Rogue_types[ 122 ];
  RogueTypeOptionalString = &Rogue_types[ 123 ];
  RogueTypeOptionalPropertyInfo = &Rogue_types[ 124 ];
  RogueTypeOptionalMethodInfo = &Rogue_types[ 125 ];
  RogueTypeOptionalCharacter = &Rogue_types[ 126 ];
  RogueTypeOptionalReal64 = &Rogue_types[ 127 ];
  RogueTypeOptionalReal32 = &Rogue_types[ 128 ];
  RogueTypeOptionalInt64 = &Rogue_types[ 129 ];
  RogueTypeFileOptions = &Rogue_types[ 130 ];
  RogueTypeOptionalTableEntry_String_Value_ = &Rogue_types[ 131 ];
  RogueTypeOptionalTypeInfo = &Rogue_types[ 132 ];
  RogueTypeOptionalTableEntry_Int32_String_ = &Rogue_types[ 133 ];
  RogueTypeOptionalTableEntry_String_String_ = &Rogue_types[ 134 ];
  RogueTypeOptionalTableEntry_String_TypeInfo_ = &Rogue_types[ 135 ];

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
  Rogue_literal_strings[130] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "name in MethodInfo._get_method_name()", 37 ) );
  Rogue_literal_strings[131] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Requirement failed:  ", 21 ) );
  Rogue_literal_strings[132] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Requirement failed.", 19 ) );
  Rogue_literal_strings[133] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "->", 2 ) );
  Rogue_literal_strings[134] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "0.0", 3 ) );
  Rogue_literal_strings[135] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "-infinity", 9 ) );
  Rogue_literal_strings[136] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "infinity", 8 ) );
  Rogue_literal_strings[137] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "NaN", 3 ) );
  Rogue_literal_strings[138] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Global", 6 ) );
  Rogue_literal_strings[139] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Value", 5 ) );
  Rogue_literal_strings[140] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "String", 6 ) );
  Rogue_literal_strings[141] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "TypeInfo", 8 ) );
  Rogue_literal_strings[142] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "StringBuilder", 13 ) );
  Rogue_literal_strings[143] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "GenericList", 11 ) );
  Rogue_literal_strings[144] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Array", 5 ) );
  Rogue_literal_strings[145] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "PropertyInfo", 12 ) );
  Rogue_literal_strings[146] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Table<<String,String>>", 22 ) );
  Rogue_literal_strings[147] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "StackTrace", 10 ) );
  Rogue_literal_strings[148] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Console", 7 ) );
  Rogue_literal_strings[149] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Math", 4 ) );
  Rogue_literal_strings[150] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "(Function())", 12 ) );
  Rogue_literal_strings[151] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "System", 6 ) );
  Rogue_literal_strings[152] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "File", 4 ) );
  Rogue_literal_strings[153] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "LineReader", 10 ) );
  Rogue_literal_strings[154] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "FileWriter", 10 ) );
  Rogue_literal_strings[155] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Scanner", 7 ) );
  Rogue_literal_strings[156] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "JSONParser", 10 ) );
  Rogue_literal_strings[157] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Table<<String,Value>>", 21 ) );
  Rogue_literal_strings[158] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "JSON", 4 ) );
  Rogue_literal_strings[159] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "ConsoleErrorPrinter", 19 ) );
  Rogue_literal_strings[160] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "(Function(String)->String)", 26 ) );
  Rogue_literal_strings[161] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Runtime", 7 ) );
  Rogue_literal_strings[162] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "StringBuilderPool", 17 ) );
  Rogue_literal_strings[163] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "WeakReference", 13 ) );
  Rogue_literal_strings[164] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "TableEntry<<String,Value>>", 26 ) );
  Rogue_literal_strings[165] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "(Function(TableEntry<<String,Value>>,TableEntry<<String,Value>>)->Logical)", 74 ) );
  Rogue_literal_strings[166] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "MethodInfo", 10 ) );
  Rogue_literal_strings[167] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Table<<Int32,String>>", 21 ) );
  Rogue_literal_strings[168] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "TableEntry<<Int32,String>>", 26 ) );
  Rogue_literal_strings[169] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "(Function(TableEntry<<Int32,String>>,TableEntry<<Int32,String>>)->Logical)", 74 ) );
  Rogue_literal_strings[170] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "TableEntry<<String,String>>", 27 ) );
  Rogue_literal_strings[171] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "(Function(TableEntry<<String,String>>,TableEntry<<String,String>>)->Logical)", 76 ) );
  Rogue_literal_strings[172] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "PrintWriterAdapter", 18 ) );
  Rogue_literal_strings[173] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "ListRewriter<<Character>>", 25 ) );
  Rogue_literal_strings[174] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "FileReader", 10 ) );
  Rogue_literal_strings[175] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "UTF8Reader", 10 ) );
  Rogue_literal_strings[176] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "TableEntry<<String,TypeInfo>>", 29 ) );
  Rogue_literal_strings[177] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "ValueList", 9 ) );
  Rogue_literal_strings[178] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "ValueTable", 10 ) );
  Rogue_literal_strings[179] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "LogicalValue", 12 ) );
  Rogue_literal_strings[180] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Real64Value", 11 ) );
  Rogue_literal_strings[181] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "NullValue", 9 ) );
  Rogue_literal_strings[182] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "StringValue", 11 ) );
  Rogue_literal_strings[183] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "TRUE", 4 ) );
  Rogue_literal_strings[184] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "yes", 3 ) );
  Rogue_literal_strings[185] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "YES", 3 ) );
  Rogue_literal_strings[186] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "1", 1 ) );
  Rogue_literal_strings[187] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "\\\"", 2 ) );
  Rogue_literal_strings[188] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "\\\\", 2 ) );
  Rogue_literal_strings[189] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "\\b", 2 ) );
  Rogue_literal_strings[190] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "\\f", 2 ) );
  Rogue_literal_strings[191] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "\\n", 2 ) );
  Rogue_literal_strings[192] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "\\r", 2 ) );
  Rogue_literal_strings[193] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "\\t", 2 ) );
  Rogue_literal_strings[194] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "\\u", 2 ) );
  Rogue_literal_strings[195] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Array<<PropertyInfo>>", 21 ) );
  Rogue_literal_strings[196] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Array<<String>>", 15 ) );
  Rogue_literal_strings[197] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Array<<(Function())>>", 21 ) );
  Rogue_literal_strings[198] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Array<<Byte>>", 13 ) );
  Rogue_literal_strings[199] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Array<<TableEntry<<String,Value>>>>", 35 ) );
  Rogue_literal_strings[200] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Array<<StringBuilder>>", 22 ) );
  Rogue_literal_strings[201] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Array<<TableEntry<<Int32,String>>>>", 35 ) );
  Rogue_literal_strings[202] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Array<<MethodInfo>>", 19 ) );
  Rogue_literal_strings[203] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Array<<Character>>", 18 ) );
  Rogue_literal_strings[204] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Array<<Value>>", 14 ) );
  Rogue_literal_strings[205] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Array<<TableEntry<<String,String>>>>", 36 ) );
  Rogue_literal_strings[206] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "PrimitiveWorkBuffer", 19 ) );
  Rogue_literal_strings[207] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "JSONParserBuffer", 16 ) );
  Rogue_literal_strings[208] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Byte[]", 6 ) );
  Rogue_literal_strings[209] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Character[]", 11 ) );
  Rogue_literal_strings[210] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Value[]", 7 ) );
  Rogue_literal_strings[211] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "PropertyInfo[]", 14 ) );
  Rogue_literal_strings[212] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "String[]", 8 ) );
  Rogue_literal_strings[213] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "(Function())[]", 14 ) );
  Rogue_literal_strings[214] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "StringBuilder[]", 15 ) );
  Rogue_literal_strings[215] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "MethodInfo[]", 12 ) );
  Rogue_literal_strings[216] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Function_221", 12 ) );
  Rogue_literal_strings[217] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Function_1182", 13 ) );
  Rogue_literal_strings[218] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Error", 5 ) );
  Rogue_literal_strings[219] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "StringTable<<String>>", 21 ) );
  Rogue_literal_strings[220] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "OutOfBoundsError", 16 ) );
  Rogue_literal_strings[221] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "RequirementError", 16 ) );
  Rogue_literal_strings[222] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "IOError", 7 ) );
  Rogue_literal_strings[223] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "JSONParseError", 14 ) );
  Rogue_literal_strings[224] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Function_281", 12 ) );
  Rogue_literal_strings[225] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "UndefinedValue", 14 ) );
  Rogue_literal_strings[226] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "StringConsolidationTable", 24 ) );
  Rogue_literal_strings[227] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "value", 5 ) );
  Rogue_literal_strings[228] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "exists", 6 ) );
  Rogue_literal_strings[229] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "(SystemEnvironment)", 19 ) );
  Rogue_literal_strings[230] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "(FileOptions)", 13 ) );
  Rogue_literal_strings[231] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Boxed", 5 ) );
  Rogue_literal_strings[232] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "ObjectValue", 11 ) );
  Rogue_literal_strings[233] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Int32Value", 10 ) );
  Rogue_literal_strings[234] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Int64Value", 10 ) );
  Rogue_literal_strings[235] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Boxed<<Int32?>>", 15 ) );
  Rogue_literal_strings[236] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Boxed<<SystemEnvironment>>", 26 ) );
  Rogue_literal_strings[237] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Boxed<<Byte?>>", 14 ) );
  Rogue_literal_strings[238] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Boxed<<Value?>>", 15 ) );
  Rogue_literal_strings[239] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Boxed<<StringBuilder?>>", 23 ) );
  Rogue_literal_strings[240] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Boxed<<(Function())?>>", 22 ) );
  Rogue_literal_strings[241] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Boxed<<String?>>", 16 ) );
  Rogue_literal_strings[242] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Boxed<<PropertyInfo?>>", 22 ) );
  Rogue_literal_strings[243] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Boxed<<MethodInfo?>>", 20 ) );
  Rogue_literal_strings[244] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Boxed<<Character?>>", 19 ) );
  Rogue_literal_strings[245] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Boxed<<Real64?>>", 16 ) );
  Rogue_literal_strings[246] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Boxed<<Real32?>>", 16 ) );
  Rogue_literal_strings[247] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Boxed<<Int64?>>", 15 ) );
  Rogue_literal_strings[248] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Boxed<<FileOptions>>", 20 ) );
  Rogue_literal_strings[249] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Boxed<<TableEntry<<String,Value>>?>>", 36 ) );
  Rogue_literal_strings[250] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Boxed<<TypeInfo?>>", 18 ) );
  Rogue_literal_strings[251] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Boxed<<TableEntry<<Int32,String>>?>>", 36 ) );
  Rogue_literal_strings[252] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Boxed<<TableEntry<<String,String>>?>>", 37 ) );
  Rogue_literal_strings[253] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Boxed<<TableEntry<<String,TypeInfo>>?>>", 39 ) );
  Rogue_literal_strings[254] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "flags", 5 ) );
  Rogue_literal_strings[255] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "PrintWriter<<global_output_buffer>>", 35 ) );
  Rogue_literal_strings[256] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "PrintWriter", 11 ) );
  Rogue_literal_strings[257] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Int32", 5 ) );
  Rogue_literal_strings[258] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Byte", 4 ) );
  Rogue_literal_strings[259] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Logical", 7 ) );
  Rogue_literal_strings[260] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Real64", 6 ) );
  Rogue_literal_strings[261] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Real32", 6 ) );
  Rogue_literal_strings[262] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Int64", 5 ) );
  Rogue_literal_strings[263] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Character", 9 ) );
  Rogue_literal_strings[264] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Reader<<Character>>", 19 ) );
  Rogue_literal_strings[265] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Reader<<String>>", 16 ) );
  Rogue_literal_strings[266] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "PrintWriter<<output_buffer>>", 28 ) );
  Rogue_literal_strings[267] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Reader<<Byte>>", 14 ) );
  Rogue_literal_strings[268] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Writer<<Byte>>", 14 ) );
  Rogue_literal_strings[269] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "PrintWriter<<buffer>>", 21 ) );
  Rogue_literal_strings[270] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Int32?", 6 ) );
  Rogue_literal_strings[271] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "SystemEnvironment", 17 ) );
  Rogue_literal_strings[272] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Byte?", 5 ) );
  Rogue_literal_strings[273] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Value?", 6 ) );
  Rogue_literal_strings[274] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "StringBuilder?", 14 ) );
  Rogue_literal_strings[275] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "(Function())?", 13 ) );
  Rogue_literal_strings[276] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "String?", 7 ) );
  Rogue_literal_strings[277] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "PropertyInfo?", 13 ) );
  Rogue_literal_strings[278] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "MethodInfo?", 11 ) );
  Rogue_literal_strings[279] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Character?", 10 ) );
  Rogue_literal_strings[280] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Real64?", 7 ) );
  Rogue_literal_strings[281] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Real32?", 7 ) );
  Rogue_literal_strings[282] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "Int64?", 6 ) );
  Rogue_literal_strings[283] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "FileOptions", 11 ) );
  Rogue_literal_strings[284] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "TableEntry<<String,Value>>?", 27 ) );
  Rogue_literal_strings[285] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "TypeInfo?", 9 ) );
  Rogue_literal_strings[286] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "TableEntry<<Int32,String>>?", 27 ) );
  Rogue_literal_strings[287] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "TableEntry<<String,String>>?", 28 ) );
  Rogue_literal_strings[288] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "TableEntry<<String,TypeInfo>>?", 30 ) );
  Rogue_literal_strings[289] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "cache", 5 ) );
  Rogue_literal_strings[290] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "console", 7 ) );
  Rogue_literal_strings[291] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "global_output_buffer", 20 ) );
  Rogue_literal_strings[292] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "exit_functions", 14 ) );
  Rogue_literal_strings[293] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "data", 4 ) );
  Rogue_literal_strings[294] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "count", 5 ) );
  Rogue_literal_strings[295] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "bin_mask", 8 ) );
  Rogue_literal_strings[296] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "cur_entry_index", 15 ) );
  Rogue_literal_strings[297] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "bins", 4 ) );
  Rogue_literal_strings[298] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "first_entry", 11 ) );
  Rogue_literal_strings[299] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "last_entry", 10 ) );
  Rogue_literal_strings[300] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "cur_entry", 9 ) );
  Rogue_literal_strings[301] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "sort_function", 13 ) );
  Rogue_literal_strings[302] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "key", 3 ) );
  Rogue_literal_strings[303] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "adjacent_entry", 14 ) );
  Rogue_literal_strings[304] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "next_entry", 10 ) );
  Rogue_literal_strings[305] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "previous_entry", 14 ) );
  Rogue_literal_strings[306] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "hash", 4 ) );
  Rogue_literal_strings[307] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "work_bytes", 10 ) );
  Rogue_literal_strings[308] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "pool", 4 ) );
  Rogue_literal_strings[309] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "utf8", 4 ) );
  Rogue_literal_strings[310] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "indent", 6 ) );
  Rogue_literal_strings[311] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "cursor_offset", 13 ) );
  Rogue_literal_strings[312] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "cursor_index", 12 ) );
  Rogue_literal_strings[313] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "at_newline", 10 ) );
  Rogue_literal_strings[314] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "available", 9 ) );
  Rogue_literal_strings[315] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "message", 7 ) );
  Rogue_literal_strings[316] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "stack_trace", 11 ) );
  Rogue_literal_strings[317] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "entries", 7 ) );
  Rogue_literal_strings[318] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "is_formatted", 12 ) );
  Rogue_literal_strings[319] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "index", 5 ) );
  Rogue_literal_strings[320] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "global_properties", 17 ) );
  Rogue_literal_strings[321] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "properties", 10 ) );
  Rogue_literal_strings[322] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "methods", 7 ) );
  Rogue_literal_strings[323] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "property_name_index", 19 ) );
  Rogue_literal_strings[324] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "property_type_index", 19 ) );
  Rogue_literal_strings[325] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "_method_name_strings", 20 ) );
  Rogue_literal_strings[326] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "base_name", 9 ) );
  Rogue_literal_strings[327] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "signature", 9 ) );
  Rogue_literal_strings[328] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "fn_ptr", 6 ) );
  Rogue_literal_strings[329] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "parameter_count", 15 ) );
  Rogue_literal_strings[330] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "call_handler", 12 ) );
  Rogue_literal_strings[331] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "_first_param_name", 17 ) );
  Rogue_literal_strings[332] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "_first_param_type", 17 ) );
  Rogue_literal_strings[333] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "_return_type", 12 ) );
  Rogue_literal_strings[334] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "position", 8 ) );
  Rogue_literal_strings[335] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "error", 5 ) );
  Rogue_literal_strings[336] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "immediate_mode", 14 ) );
  Rogue_literal_strings[337] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "is_blocking", 11 ) );
  Rogue_literal_strings[338] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "decode_bytes", 12 ) );
  Rogue_literal_strings[339] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "output_buffer", 13 ) );
  Rogue_literal_strings[340] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "input_buffer", 12 ) );
  Rogue_literal_strings[341] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "next_input_character", 20 ) );
  Rogue_literal_strings[342] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "input_bytes", 11 ) );
  Rogue_literal_strings[343] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "termios original_terminal_settings;", 35 ) );
  Rogue_literal_strings[344] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "int     original_stdin_flags;", 29 ) );
  Rogue_literal_strings[345] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "command_line_arguments", 22 ) );
  Rogue_literal_strings[346] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "executable_filepath", 19 ) );
  Rogue_literal_strings[347] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "environment", 11 ) );
  Rogue_literal_strings[348] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "source", 6 ) );
  Rogue_literal_strings[349] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "next", 4 ) );
  Rogue_literal_strings[350] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "buffer", 6 ) );
  Rogue_literal_strings[351] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "prev", 4 ) );
  Rogue_literal_strings[352] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "FILE* fp;", 9 ) );
  Rogue_literal_strings[353] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "line", 4 ) );
  Rogue_literal_strings[354] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "column", 6 ) );
  Rogue_literal_strings[355] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "spaces_per_tab", 14 ) );
  Rogue_literal_strings[356] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "reader", 6 ) );
  Rogue_literal_strings[357] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "next_weak_reference", 19 ) );
  Rogue_literal_strings[358] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "RogueObject* value;", 19 ) );
  Rogue_literal_strings[359] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "output", 6 ) );
  Rogue_literal_strings[360] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "true_value", 10 ) );
  Rogue_literal_strings[361] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "false_value", 11 ) );
  Rogue_literal_strings[362] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "empty_string", 12 ) );
  Rogue_literal_strings[363] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "list", 4 ) );
  Rogue_literal_strings[364] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "read_index", 10 ) );
  Rogue_literal_strings[365] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "write_index", 11 ) );
  Rogue_literal_strings[366] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "buffer_position", 15 ) );
  Rogue_literal_strings[367] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "byte_reader", 11 ) );
  Rogue_literal_strings[368] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "arg", 3 ) );
  Rogue_literal_strings[369] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "pattern", 7 ) );
  Rogue_literal_strings[370] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "file", 4 ) );
  Rogue_literal_strings[371] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "parser", 6 ) );
  Rogue_literal_strings[372] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "fn", 2 ) );
  Rogue_literal_strings[373] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "other", 5 ) );
  Rogue_literal_strings[374] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "formatted", 9 ) );
  Rogue_literal_strings[375] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "omit_commas", 11 ) );
  Rogue_literal_strings[376] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "new_value", 9 ) );
  Rogue_literal_strings[377] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "table_key_or_list_value", 23 ) );
  Rogue_literal_strings[378] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "bin_count", 9 ) );
  Rogue_literal_strings[379] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "entry", 5 ) );
  Rogue_literal_strings[380] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "i1", 2 ) );
  Rogue_literal_strings[381] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "n", 1 ) );
  Rogue_literal_strings[382] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "_key", 4 ) );
  Rogue_literal_strings[383] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "_value", 6 ) );
  Rogue_literal_strings[384] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "_hash", 5 ) );
  Rogue_literal_strings[385] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "ch", 2 ) );
  Rogue_literal_strings[386] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "st", 2 ) );
  Rogue_literal_strings[387] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "substring", 9 ) );
  Rogue_literal_strings[388] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "at_index", 8 ) );
  Rogue_literal_strings[389] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "i2", 2 ) );
  Rogue_literal_strings[390] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "spaces", 6 ) );
  Rogue_literal_strings[391] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "fill", 4 ) );
  Rogue_literal_strings[392] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "optional_i1", 11 ) );
  Rogue_literal_strings[393] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "starting_index", 14 ) );
  Rogue_literal_strings[394] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "quantity", 8 ) );
  Rogue_literal_strings[395] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "look_for", 8 ) );
  Rogue_literal_strings[396] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "replace_with", 12 ) );
  Rogue_literal_strings[397] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "separator", 9 ) );
  Rogue_literal_strings[398] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "width", 5 ) );
  Rogue_literal_strings[399] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "allow_break_after", 17 ) );
  Rogue_literal_strings[400] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "param1", 6 ) );
  Rogue_literal_strings[401] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "param2", 6 ) );
  Rogue_literal_strings[402] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "initial_capacity", 16 ) );
  Rogue_literal_strings[403] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "decimal_places", 14 ) );
  Rogue_literal_strings[404] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "additional_bytes", 16 ) );
  Rogue_literal_strings[405] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "desired_capacity", 16 ) );
  Rogue_literal_strings[406] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "minimum_count", 13 ) );
  Rogue_literal_strings[407] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "before_index", 12 ) );
  Rogue_literal_strings[408] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "additional_elements", 19 ) );
  Rogue_literal_strings[409] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "element_count", 13 ) );
  Rogue_literal_strings[410] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "delta", 5 ) );
  Rogue_literal_strings[411] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "_auto_75", 8 ) );
  Rogue_literal_strings[412] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "omit_count", 10 ) );
  Rogue_literal_strings[413] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "map_fn", 6 ) );
  Rogue_literal_strings[414] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "decimal_digits", 14 ) );
  Rogue_literal_strings[415] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "base", 4 ) );
  Rogue_literal_strings[416] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "digits", 6 ) );
  Rogue_literal_strings[417] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "start", 5 ) );
  Rogue_literal_strings[418] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "allow_dollar", 12 ) );
  Rogue_literal_strings[419] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "_auto_98", 8 ) );
  Rogue_literal_strings[420] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "_auto_99", 8 ) );
  Rogue_literal_strings[421] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "global_property_name_index", 26 ) );
  Rogue_literal_strings[422] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "global_property_type_index", 26 ) );
  Rogue_literal_strings[423] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "method_index", 12 ) );
  Rogue_literal_strings[424] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "_auto_126", 9 ) );
  Rogue_literal_strings[425] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "_auto_127", 9 ) );
  Rogue_literal_strings[426] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "_auto_128", 9 ) );
  Rogue_literal_strings[427] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "_auto_318", 9 ) );
  Rogue_literal_strings[428] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "_auto_319", 9 ) );
  Rogue_literal_strings[429] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "_auto_320", 9 ) );
  Rogue_literal_strings[430] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "_auto_321", 9 ) );
  Rogue_literal_strings[431] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "_auto_322", 9 ) );
  Rogue_literal_strings[432] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "_auto_323", 9 ) );
  Rogue_literal_strings[433] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "_auto_129", 9 ) );
  Rogue_literal_strings[434] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "_auto_231", 9 ) );
  Rogue_literal_strings[435] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "_auto_234", 9 ) );
  Rogue_literal_strings[436] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "bytes", 5 ) );
  Rogue_literal_strings[437] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "_filepath", 9 ) );
  Rogue_literal_strings[438] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "append", 6 ) );
  Rogue_literal_strings[439] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "_auto_243", 9 ) );
  Rogue_literal_strings[440] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "_auto_249", 9 ) );
  Rogue_literal_strings[441] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "preserve_crlf", 13 ) );
  Rogue_literal_strings[442] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "json", 4 ) );
  Rogue_literal_strings[443] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "_auto_255", 9 ) );
  Rogue_literal_strings[444] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "open_ch", 7 ) );
  Rogue_literal_strings[445] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "close_ch", 8 ) );
  Rogue_literal_strings[446] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "_buffer", 7 ) );
  Rogue_literal_strings[447] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "_auto_325", 9 ) );
  Rogue_literal_strings[448] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "_auto_326", 9 ) );
  Rogue_literal_strings[449] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "_auto_327", 9 ) );
  Rogue_literal_strings[450] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "_auto_328", 9 ) );
  Rogue_literal_strings[451] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "_auto_329", 9 ) );
  Rogue_literal_strings[452] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "_auto_331", 9 ) );
  Rogue_literal_strings[453] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "_auto_444", 9 ) );
  Rogue_literal_strings[454] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "limit", 5 ) );
  Rogue_literal_strings[455] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "requirement", 11 ) );
  Rogue_literal_strings[456] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "_auto_968", 9 ) );
  Rogue_literal_strings[457] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "_auto_1128", 10 ) );
  Rogue_literal_strings[458] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "_auto_1183", 10 ) );
  Rogue_literal_strings[459] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "_auto_1209", 10 ) );
  Rogue_literal_strings[460] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "_auto_1212", 10 ) );
  Rogue_literal_strings[461] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "_auto_1236", 10 ) );
  Rogue_literal_strings[462] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "_auto_1255", 10 ) );
  Rogue_literal_strings[463] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "_auto_1258", 10 ) );
  Rogue_literal_strings[464] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "_auto_1264", 10 ) );
  Rogue_literal_strings[465] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "_auto_1342", 10 ) );
  Rogue_literal_strings[466] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "_auto_1351", 10 ) );
  Rogue_literal_strings[467] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "_auto_1360", 10 ) );
  Rogue_literal_strings[468] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "_auto_1474", 10 ) );
  Rogue_literal_strings[469] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "_auto_1483", 10 ) );
  Rogue_literal_strings[470] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "_auto_1629", 10 ) );
  Rogue_literal_strings[471] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "_auto_1647", 10 ) );
  Rogue_literal_strings[472] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "_auto_1650", 10 ) );
  Rogue_literal_strings[473] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "_auto_1653", 10 ) );
  Rogue_literal_strings[474] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "_auto_1728", 10 ) );
  Rogue_literal_strings[475] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "_auto_1740", 10 ) );
  Rogue_literal_strings[476] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "_auto_1758", 10 ) );
  Rogue_literal_strings[477] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "_auto_1848", 10 ) );
  Rogue_literal_strings[478] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "_auto_1869", 10 ) );
  Rogue_literal_strings[479] = (RogueString*) RogueObject_retain( RogueString_create_from_utf8( "_auto_1887", 10 ) );

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
  RogueMethodInfo__init_class();
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
