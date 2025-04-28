	.text

/* Switch from current_thread to next_thread. Make next_thread
 * the current_thread, and set next_thread to 0.
 * Use eax as a temporary register; it is caller saved.
 */
	.globl thread_switch
thread_switch:
	/* Save current thread context */
	pushal			/* Push all registers onto the stack */

	/* Save current stack pointer */
	movl current_thread, %eax
	movl %esp, (%eax)	/* Save ESP to current_thread->sp */

	/* Load next thread's stack pointer */
	movl next_thread, %eax
	movl %eax, current_thread	/* next_thread becomes current_thread */
	movl $0, next_thread		/* next_thread = 0 */
	movl (%eax), %esp	/* Load ESP from new current_thread->sp */

	/* Restore registers */
	popal			/* Pop all registers from the stack */

	ret				/* pop return address from stack */
