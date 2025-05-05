#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"

// 声明 mappages 函数
int mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm);

// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256];
extern uint vectors[];  // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;

void
tvinit(void)
{
  int i;

  for(i = 0; i < 256; i++)
    SETGATE(idt[i], 0, SEG_KCODE<<3, vectors[i], 0);
  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE<<3, vectors[T_SYSCALL], DPL_USER);

  initlock(&tickslock, "time");
}

void
idtinit(void)
{
  lidt(idt, sizeof(idt));
}

//PAGEBREAK: 41
void
trap(struct trapframe *tf)
{
  if(tf->trapno == T_SYSCALL){
    if(myproc()->killed)
      exit();
    myproc()->tf = tf;
    syscall();
    if(myproc()->killed)
      exit();
    return;
  }

  switch(tf->trapno){
  case T_IRQ0 + IRQ_TIMER:
    if(cpuid() == 0){
      acquire(&tickslock);
      ticks++;
      wakeup(&ticks);
      release(&tickslock);
    }
    // 处理 alarm
    struct proc *p = myproc();
    if(p != 0 && (tf->cs & 3) == 3) { // 确保进程正在运行且中断来自用户空间
      if(p->alarmticks > 0) { // 确保 alarm 已设置
        p->ticksleft--;
        if(p->ticksleft <= 0) { // 时间到，调用处理函数
          // 重置计时器
          p->ticksleft = p->alarmticks;
          
          // 直接调用 cprintf 打印 "alarm!" 消息
          cprintf("alarm!\n");
        }
      }
    }
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE:
    ideintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE+1:
    // Bochs generates spurious IDE1 interrupts.
    break;
  case T_IRQ0 + IRQ_KBD:
    kbdintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_COM1:
    uartintr();
    lapiceoi();
    break;
  case T_IRQ0 + 7:
  case T_IRQ0 + IRQ_SPURIOUS:
    cprintf("cpu%d: spurious interrupt at %x:%x\n",
            cpuid(), tf->cs, tf->eip);
    lapiceoi();
    break;

  //PAGEBREAK: 13
  default:
    if(myproc() == 0 || (tf->cs&3) == 0){
      // In kernel, it must be our mistake.
      cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
              tf->trapno, cpuid(), tf->eip, rcr2());
      panic("trap");
    }
    
    // 处理页面错误（懒分配）
    if(tf->trapno == T_PGFLT) {
      uint va = PGROUNDDOWN(rcr2()); // 获取导致页面错误的虚拟地址并向下取整到页面边界
      struct proc *p = myproc();
      
      // 检查地址是否在进程的地址空间内
      if(va < p->sz) {
        char *mem = kalloc(); // 分配一页物理内存
        if(mem == 0) {
          cprintf("lazy alloc: out of memory\n");
          p->killed = 1;
          break;
        }
        
        // 清零新分配的内存页
        memset(mem, 0, PGSIZE);
        
        // 将新分配的物理页映射到虚拟地址空间
        if(mappages(p->pgdir, (char*)va, PGSIZE, V2P(mem), PTE_W|PTE_U) < 0) {
          cprintf("lazy alloc: mappages failed\n");
          kfree(mem);
          p->killed = 1;
          break;
        }
        
        // 成功处理页面错误，返回继续执行
        return;
      }
    }
    
    // In user space, assume process misbehaved.
    cprintf("pid %d %s: trap %d err %d on cpu %d "
            "eip 0x%x addr 0x%x--kill proc\n",
            myproc()->pid, myproc()->name, tf->trapno,
            tf->err, cpuid(), tf->eip, rcr2());
    myproc()->killed = 1;
  }

  // Force process exit if it has been killed and is in user space.
  // (If it is still executing in the kernel, let it keep running
  // until it gets to the regular system call return.)
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();

  // Force process to give up CPU on clock tick.
  // If interrupts were on while locks held, would need to check nlock.
  if(myproc() && myproc()->state == RUNNING &&
     tf->trapno == T_IRQ0+IRQ_TIMER)
    yield();

  // Check if the process has been killed since we yielded
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();
}
