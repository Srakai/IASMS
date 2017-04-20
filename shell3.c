#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ncurses.h>
#include <sys/mman.h>

#define BUFF_SIZE 1024
#define PAGE_SIZE 4096

const char *jmp_r15 = "\x41\xff\xe7"; // jmp r15
const char *jmp_r15p144 = "\x41\xff\xa7\x90\x00\x00\x00"; //jmp 144(%%r15)

struct registers
{					//offset in bytes
	uint64_t rax;	//0
	uint64_t rbx;	//8
	uint64_t rcx;	//16
	uint64_t rdx;	//24
	uint64_t rbp;	//32
	uint64_t rsp;	//40
	uint64_t rip;	//48
	uint64_t rsi;	//56
	uint64_t rdi;	//64
	uint64_t r8;	//72
	uint64_t r9;	//80
	uint64_t r10;	//88
	uint64_t r11;	//96
	uint64_t r12;	//104
	uint64_t r13;	//112
	uint64_t r14;	//120
	uint64_t r15;	//128
	uint64_t rflags;//136
	
	uint64_t org_rip;	//144
	uint64_t org_struct;//152

} usr_reg, org_reg;

// loads addres to r15
#define L_R15(x) \
do{\
	asm("movq %0, %%r15"::"r" (x):"r15");\
}while(0)

// saves all registers to 'register' struct, struct addres in %%r15
#define SAVE_REG \
do{\
	asm("mov %%rax, 0(%%r15)\n"\
		"mov %%rbx, 8(%%r15)\n"\
		"mov %%rcx, 16(%%r15)\n"\
		"mov %%rdx, 24(%%r15)\n"\
		"mov %%rbp, 32(%%r15)\n"\
		"mov %%rsp, 40(%%r15)\n"\
/*		"mov %%rip, 48(%%r15)\n" dont save rip*/\
		"mov %%rsi, 56(%%r15)\n"\
		"mov %%rdi, 64(%%r15)\n"\
		"mov %%r8,	72(%%r15)\n"\
		"mov %%r9,	80(%%r15)\n"\
		"mov %%r10, 88(%%r15)\n"\
		"mov %%r11, 96(%%r15)\n"\
		"mov %%r12, 104(%%r15)\n"\
		"mov %%r13, 112(%%r15)\n"\
		"mov %%r14, 120(%%r15)\n"\
	:::"r15");\
}while(0)

// loads all registers from 'register' struct, struct addres in %%r15
#define LOAD_REG \
do{\
	asm("mov 0(%%r15), %%rax\n"\
		"mov 8(%%r15), %%rbx\n"\
		"mov 16(%%r15), %%rcx\n"\
		"mov 24(%%r15), %%rdx\n"\
		"mov 32(%%r15), %%rbp\n"\
		"mov 40(%%r15), %%rsp\n"\
/*		"mov 48(%%r15), %%rip\n" dont load rip*/\
		"mov 56(%%r15), %%rsi\n"\
		"mov 64(%%r15), %%rdi\n"\
		"mov 72(%%r15), %%r8\n"\
		"mov 80(%%r15), %%r9\n"\
		"mov 88(%%r15), %%r10\n"\
		"mov 96(%%r15), %%r11\n"\
		"mov 104(%%r15), %%r12\n"\
		"mov 112(%%r15), %%r13\n"\
		"mov 120(%%r15), %%r14\n"\
	:::"r15");\
}while(0)

#define LOAD_RIP \
do{\
	asm("jmp *48(%%r15)":::"r15");\
}while(0)

// this changes %%r15 to point to orginal struct
#define EXCH_R15 \
do{\
	asm("mov 152(%%r15), %%r15":::"r15");\
}while(0)

void select_box(WINDOW **, int);
void print_mem(WINDOW **, int, void *, int);
void print_mem_rev(WINDOW **, int, void *, int);
void print_reg(WINDOW **, int, struct registers *);

int main(void){
	
	FILE *n_out, *n_in;
	
	char *ex_nasm =  "nasm /tmp/n_in -o /tmp/n_out 2>/dev/null";
	int n_out_s;

	void *stack_segment, *text_segment;
	
	// crate pseudo stack
	stack_segment = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, 0, 0);
	
	// crate .text segment
	text_segment = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANONYMOUS | MAP_PRIVATE, 0, 0);
	
	char buffer[BUFF_SIZE];
	
	// ncurses init
	int ch, row, col;

	initscr();
	getmaxyx(stdscr, row, col);
	clear();
	echo();
	cbreak();
	timeout(-1);
	nodelay(stdscr, FALSE);

	WINDOW *win1 = newwin(row-2, col/3, 0, 0);                  // window for instructions 
	WINDOW *win2 = newwin(row-2, col/3+2, 0, col/3-1); 	 		// stack
	WINDOW *win3 = newwin(row-2, col/3, 0, col*2/3);          	// and registers
	WINDOW *win4 = newwin(2, col, row-2, 1);          	
	
	WINDOW *windows[4] = {win1, win2, win3, win4};
	

	// prepare registers
	usr_reg.rsp = (uint64_t) (stack_segment + PAGE_SIZE- row*8);	// lets give user more space
	usr_reg.rip = (uint64_t) text_segment;
	usr_reg.org_struct = (uint64_t) &org_reg;
	org_reg.org_struct = (uint64_t) &usr_reg;



	int code_offset = 3; //normally code_offset += size of instruction;
	void *instruction_end = (char *) (text_segment + code_offset);
	size_t instruction_offset =0;


	// set %%r15 to point to org_reg	
	L_R15(&org_reg);
	// save orginal registers
	SAVE_REG;

	// set %%r15 to point to usr_reg
	// and save us_regs
	// it's just to enter the loop properly		
	EXCH_R15;
	LOAD_REG;
	
	// this saves our rip it will create pseudo loop	
	asm("call rape\n"
		"back:");
	// program will return here after executing user instruction
	// save usr_registers
	SAVE_REG;	
		
	// change r15 to point to orginal struct and load orginal regs
	EXCH_R15;
	LOAD_REG;

	asm("on_err:");
	// do some printing here, take istructions
	mvwprintw(windows[1], row-1, 1, "%s", buffer); wrefresh(windows[1]);
	print_reg(windows, 2, &usr_reg);	//print registers
	print_mem_rev(windows, 0, (void *)(usr_reg.rsp ), row);	//print stack
	print_mem(windows, 1, text_segment , row);	//print .text

	select_box(windows, -1);


	//get usr input
	wclear(windows[3]);
	mvwprintw(windows[3], 0, 0, "ASM_SHELL >>", buffer); wrefresh(windows[3]);
	wgetstr(windows[3], buffer);
	
	//endwin();
	
	
	//fgets(buffer, BUFF_SIZE, stdin);
	
	n_in = fopen("/tmp/n_in", "wt");
	fprintf(n_in, "[BITS 64]\n");		// needed for nasm
	fprintf(n_in, "%s", buffer);		// now we have input file to nasm
	fclose(n_in);

	system(ex_nasm);

	n_out = fopen("/tmp/n_out", "rb");
	if(n_out== NULL)
	{
		asm("jmp on_err"); //continue loop 
	}

	fgets(text_segment + instruction_offset, BUFF_SIZE, n_out);	//put istruction after previous instruction
	
	//set rip to current istruction
	usr_reg.rip = (uint64_t) text_segment + instruction_offset;
	
	//get instruction size
	fseek(n_out, 0L, SEEK_END);
	n_out_s = ftell(n_out);
	if(n_out_s<1)
	{
		fclose(n_out);
		asm("jmp on_err"); //continue loop 
	}
	instruction_offset += ftell(n_out);
	fclose(n_out);
	
	//put jump on end	
	memcpy(text_segment + instruction_offset, jmp_r15p144, sizeof(jmp_r15p144));
	
	//save orginal registers
	L_R15(&org_reg);
	SAVE_REG;	
	
	L_R15(&usr_reg);
	LOAD_REG;		//we cannot use orginal stack after this point
	//jump to our code
	LOAD_RIP;


	asm("rape: pop %0":"=r"(usr_reg.org_rip)::);
	// not necesary
	asm("xor %%rax, %%rax;\n push %%rax":::"rax");	// this is only of cleanup, its done only in first iteration
	asm("jmp back":::); //save rip to 
		
	return 0;
}

void select_box(WINDOW **windows, int w)
{
	
	for(int i=0;i<3;i++)
	{
		box(windows[i], '|', '-');
		wrefresh(windows[i]);
	}
	if(w<0) return;		//dont select any window	
	box(windows[w], '#', '#');
	wrefresh(windows[w]);

}
void print_mem_rev(WINDOW **windows, int w, void *addr, int row)
{
	
	wclear(windows[w]);
	int pos = 0, m_rows = row-5;	// yeah it's little hardcoded 
	unsigned char *pr_addr =  addr - (m_rows/2)*8;	// in each row print 8 bytes
	const char *addr_format = "[0x%lx]";
	const char *t_addr_format = "{0x%lx}";	//addr on stack pointer

	for(int i=m_rows-1;i>=0;i--)
	{
		if(pr_addr == addr)
		{
			mvwprintw(windows[w], i+1, 1, t_addr_format, pr_addr);
		}
		else
		{
			mvwprintw(windows[w], i+1, 1, addr_format, pr_addr);
		}
		pos += 18;
		
		for(int j=7;j>=0;j--)
		{
				mvwprintw(windows[w], i+1, 1 + pos, "%02x", pr_addr[j]);
				pos+= 3; 

		}
		pr_addr += 8;
		pos =0;
	}	
	wrefresh(windows[w]);

}
void print_mem(WINDOW **windows, int w, void *addr, int row)
{
	
	wclear(windows[w]);
	int pos = 0, m_rows = row-5;	// little hardcoded 
	unsigned char *pr_addr = (char *) addr;	// in each row print 8 bytes
	const char *addr_format = "[0x%lx]";

	for(int i=0;i<m_rows;i++)
	{
		mvwprintw(windows[w], i+1, 1, addr_format, pr_addr);
		pos += 18;
		
		for(int j=0;j<8;j++)
		{
				mvwprintw(windows[w], i+1, 1 + pos, "%02x", pr_addr[j]);
				pos+= 3; 
		}
		pr_addr += 8;
		pos =0;
	}	
	wrefresh(windows[w]);

}
void print_reg(WINDOW **windows, int w, struct registers *reg)
{
	wclear(windows[w]);

	mvwprintw(windows[w], 1, 1, "%s\t0x%lx", "rax:", reg->rax);
	mvwprintw(windows[w], 2, 1, "%s\t0x%lx", "rbx:", reg->rbx);
	mvwprintw(windows[w], 3, 1, "%s\t0x%lx", "rcx:", reg->rcx);
	mvwprintw(windows[w], 4, 1, "%s\t0x%lx", "rdx:", reg->rdx);
	mvwprintw(windows[w], 5, 1, "%s\t0x%lx", "rbp:", reg->rbp);
	mvwprintw(windows[w], 6, 1, "%s\t0x%lx", "rsp:", reg->rsp);
	mvwprintw(windows[w], 7, 1, "%s\t0x%lx", "rip:", reg->rip);
	mvwprintw(windows[w], 8, 1, "%s\t0x%lx", "rsi:", reg->rsi);
	mvwprintw(windows[w], 9, 1, "%s\t0x%lx", "rdi:", reg->rdi);
	mvwprintw(windows[w], 10, 1, "%s\t0x%lx", "r8:", reg->r8);
	mvwprintw(windows[w], 11, 1, "%s\t0x%lx", "r9:", reg->r9);
	mvwprintw(windows[w], 12, 1, "%s\t0x%lx", "r10:", reg->r10);
	mvwprintw(windows[w], 13, 1, "%s\t0x%lx", "r11:", reg->r11);
	mvwprintw(windows[w], 14, 1, "%s\t0x%lx", "r12:", reg->r12);
	mvwprintw(windows[w], 15, 1, "%s\t0x%lx", "r13:", reg->r13);
	mvwprintw(windows[w], 16, 1, "%s\t0x%lx", "r14:", reg->r14);
	mvwprintw(windows[w], 17, 1, "%s\t0x%lx", "r15:", reg->r15);
	mvwprintw(windows[w], 18, 1, "%s\t0x%lx", "flags:", reg->rflags);
	
	wrefresh(windows[w]);
}

