#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <math.h>

static size_t page_size;

// align_down - rounds a value down to an alignment
// @x: the value
// @a: the alignment (must be power of 2)
//
// Returns an aligned value.
#define align_down(x, a) ((x) & ~((typeof(x))(a) - 1))

#define AS_LIMIT	(1 << 25) // Maximum limit on virtual memory bytes
#define MAX_SQRTS	(1 << 27) // Maximum limit on sqrt table entries
static double *sqrts;

// Use this helper function as an oracle for square root values.
static void
calculate_sqrts(double *sqrt_pos, int start, int nr)
{
  int i;

  for (i = 0; i < nr; i++)
    sqrt_pos[i] = sqrt((double)(start + i));
}

static void
handle_sigsegv(int sig, siginfo_t *si, void *ctx)
{
  // Get the page-aligned address where the fault occurred
  uintptr_t fault_addr = (uintptr_t)si->si_addr;
  uintptr_t page_addr = align_down(fault_addr, page_size);
  
  // Calculate the index of the first element in the faulting page
  size_t elem_size = sizeof(double);
  size_t index_in_array = (fault_addr - (uintptr_t)sqrts) / elem_size;
  size_t first_index_in_page = align_down(index_in_array, page_size / elem_size);
  
  // Calculate how many elements fit in a page
  size_t elems_per_page = page_size / elem_size;
  
  // Check if the fault address is within our sqrt table range
  if (fault_addr < (uintptr_t)sqrts || 
      fault_addr >= (uintptr_t)sqrts + MAX_SQRTS * elem_size) {
    printf("SIGSEGV at address outside sqrt table: 0x%lx\n", fault_addr);
    exit(EXIT_FAILURE);
  }
  
  // Static variable to keep track of the last mapped page
  static void *last_mapped_page = NULL;
  
  // Unmap the last page if it exists and is different from the current page
  if (last_mapped_page != NULL && last_mapped_page != (void*)page_addr) {
    if (munmap(last_mapped_page, page_size) == -1) {
      fprintf(stderr, "Failed to unmap previous page at %p: %s\n", 
              last_mapped_page, strerror(errno));
      exit(EXIT_FAILURE);
    }
  }
  
  // Map a new page at the faulting address
  void *mapped_addr = mmap((void*)page_addr, page_size, 
                          PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
                          -1, 0);
  
  if (mapped_addr == MAP_FAILED) {
    fprintf(stderr, "Failed to map memory at %p: %s\n", 
            (void*)page_addr, strerror(errno));
    exit(EXIT_FAILURE);
  }
  
  // Calculate the square roots for this page
  calculate_sqrts((double*)mapped_addr, first_index_in_page, elems_per_page);
  
  // Update the last mapped page
  last_mapped_page = mapped_addr;
}

static void
setup_sqrt_region(void)
{
  struct rlimit lim = {AS_LIMIT, AS_LIMIT};
  struct sigaction act;

  // Only mapping to find a safe location for the table.
  sqrts = mmap(NULL, MAX_SQRTS * sizeof(double) + AS_LIMIT, PROT_NONE,
	       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (sqrts == MAP_FAILED) {
    fprintf(stderr, "Couldn't mmap() region for sqrt table; %s\n",
	    strerror(errno));
    exit(EXIT_FAILURE);
  }

  // Now release the virtual memory to remain under the rlimit.
  if (munmap(sqrts, MAX_SQRTS * sizeof(double) + AS_LIMIT) == -1) {
    fprintf(stderr, "Couldn't munmap() region for sqrt table; %s\n",
            strerror(errno));
    exit(EXIT_FAILURE);
  }

  // Set a soft rlimit on virtual address-space bytes.
  if (setrlimit(RLIMIT_AS, &lim) == -1) {
    fprintf(stderr, "Couldn't set rlimit on RLIMIT_AS; %s\n", strerror(errno));
    exit(EXIT_FAILURE);
  }

  // Register a signal handler to capture SIGSEGV.
  act.sa_sigaction = handle_sigsegv;
  act.sa_flags = SA_SIGINFO;
  sigemptyset(&act.sa_mask);
  if (sigaction(SIGSEGV, &act, NULL) == -1) {
    fprintf(stderr, "Couldn't set up SIGSEGV handler;, %s\n", strerror(errno));
    exit(EXIT_FAILURE);
  }
}

static void
test_sqrt_region(void)
{
  int i, pos = rand() % (MAX_SQRTS - 1);
  double correct_sqrt;

  printf("Validating square root table contents...\n");
  srand(0xDEADBEEF);

  for (i = 0; i < 500000; i++) {
    if (i % 2 == 0)
      pos = rand() % (MAX_SQRTS - 1);
    else
      pos += 1;
    calculate_sqrts(&correct_sqrt, pos, 1);
    if (sqrts[pos] != correct_sqrt) {
      fprintf(stderr, "Square root is incorrect. Expected %f, got %f.\n",
              correct_sqrt, sqrts[pos]);
      exit(EXIT_FAILURE);
    }
  }

  printf("All tests passed!\n");
}

int
main(int argc, char *argv[])
{
  page_size = sysconf(_SC_PAGESIZE);
  printf("page_size is %ld\n", page_size);
  setup_sqrt_region();
  test_sqrt_region();
  return 0;
}
