#include "userprog/syscall.h"
//#include "userprog/process.h"
//#include "filesys/filesys.c"
//#include "filesys/file.c"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
//#include "devices/shutdown.c"
/*
static struct file{
struct inode *inode;
off_t pos;
bool deny_write;
};
*/
static struct lock filesys_lock;
static void syscall_handler(struct intr_frame *);
void halt(void);
int wait(pid_t pid);
void exit(int status);
pid_t exec(const char *cmd_line);
bool create(const char *file, unsigned initial_size);
bool remove(const char *file);
int open(const char *file);
int filesize(int fd);
int read(int fd, void *buffer, unsigned size);
int write(int fd, const void *buffer, unsigned size);
void seek(int fd, unsigned position);
unsigned tell(int fd);
void close(int fd);


void
syscall_init(void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
 //added
 lock_init(&filesys_lock);

}

//added
void check_pointer(void* pointer){
    if(pointer==NULL)
        exit(-1);
    if(!is_user_vaddr(pointer))
        exit(-1);
}


static void
syscall_handler(struct intr_frame *f UNUSED) 
{
  uint32_t * ff = f->esp;
  switch(*(uint32_t*)(f->esp)){

  case SYS_HALT:                   /* Halt the operating system. */
	halt();
	break;
  case SYS_EXIT:                   /* Terminate this process. */
	check_pointer(f->esp + 4);
	exit((int) *(uint32_t*)(f->esp+4));
	break;
  case SYS_EXEC:                   /* Start another process. */
	check_pointer(f->esp + 4);
	f->eax = exec((const char*) *(uint32_t*)(f->esp + 4));
	break;
  case SYS_WAIT:                   /* Wait for a child process to die. */
	check_pointer(f->esp + 4);
	f->eax = wait((pid_t)*(uint32_t*)(f->esp+4));
	break;
  case SYS_CREATE:                 /* Create a file. */
        check_pointer(f->esp + 4);
	f->eax = create((const char *)ff[1],(unsigned)ff[2]);
	break;
  case SYS_REMOVE:                 /* Delete a file. */
        check_pointer(f->esp + 4);
	f->eax = remove((const char *)ff[1]);
	break;
  case SYS_OPEN:                   /* Open a file. */
        check_pointer(f->esp + 4);
	f->eax = open((const char *) ff[1]);
	break;
  case SYS_FILESIZE:               /* Obtain a file's size. */
        check_pointer(f->esp + 4);
	f->eax = filesize((int)ff[1]);
	break;
  case SYS_READ:                   /* Read from a file. */
        check_pointer(f->esp + 4);
	f->eax = read((int)ff[1],(void*)ff[2],(unsigned)ff[3]);
	break;
  case SYS_WRITE:                  /* Write to a file. */
        check_pointer(f->esp + 4);
	f->eax = write((int)ff[1], (void*)ff[2],(unsigned)ff[3]);
	break;
  case SYS_SEEK:                   /* Change position in a file. */
        check_pointer(f->esp + 4);
	seek((int)ff[1],(unsigned)ff[2]);
	break;
  case SYS_TELL:                   /* Report current position in a file. */
        check_pointer(f->esp + 4);
	f->eax = tell((int)ff[1]);
	break;
  case SYS_CLOSE:                  /* Close a file. */
        check_pointer(f->esp + 4);
	close((int)ff[1]);
	break;
  default:
	printf("system call error\n");
	exit(-1);
  }
  printf ("system call!\n");
  thread_exit ();
}

//all below added for lab2
void halt(void){
    shutdown_power_off();
}

int wait(pid_t pid){
    struct thread *temp_thread=NULL;
    struct list_elem *temp_elem=NULL;
    int rtrn=-1;
    for(temp_elem=list_begin(&thread_current()->child);
        temp_elem!=list_end(&thread_current()->child);
        temp_elem=list_next(temp_elem)){
            //looping through the child list to find the one with pid
            temp_thread=list_entry(temp_elem,struct thread, child_elem);
            if(temp_thread->tid==pid && !thread_current()->called_wait){
                //if pid terminated (and called exit) return the status when it exited
                //and remove from child thread list
                if(temp_thread->called_exit){
                    rtrn=temp_thread->exit_status;
                    list_remove(&temp_thread->child_elem);
                }
                //if pid is not alive and did not call exit return -1
                else if(temp_thread->status!=0)
                    rtrn=-1;
                else{
                thread_current()->called_wait=true;
                rtrn=process_wait(temp_thread->tid);
                sema_down(&thread_current()->sema_wait);
		 list_remove(&temp_thread->child_elem);
                thread_current()->called_wait=false;
                 }
            }
        }
    return rtrn;
}

void exit(int status){
    struct thread *cur = thread_current ();
    int i;
    //closing any opened files before exiting 
    for(i=3;i<64;i++){
        if(thread_current()->fdt[i]!=NULL)
            close(i);
    }
    printf("%s: exit(%d)\n",cur->name, status);
    thread_current()->exit_status=status;
    thread_current()->called_exit=true;
    thread_exit();

}

pid_t exec(const char *cmd_line){
    tid_t pid=process_execute(cmd_line);
    sema_down(&(thread_current()->sema_exec));
	return pid;
}

bool create(const char *file, unsigned initial_size){
    //check address of file (to do)
    check_pointer(file);
    return filesys_create(file,initial_size);
}

bool remove(const char *file){
    //check validity of address (to do)
    check_pointer(file);
    return filesys_remove(file);
}

int open(const char *file){
    if(file==NULL)
	exit(-1);
    check_pointer(file);
    //put a lock on the file
    lock_acquire(&filesys_lock);
    int rtrn=-1;
    int i;
    struct file *openedFile = filesys_open(file);
    if(openedFile==NULL){
        rtrn=-1;
    } else{
        for (i=3; i<64;i++){
            //if found an empty fd & the thread name == file name
            if((thread_current()->fdt[i]==NULL)){
                file_deny_write(openedFile); //stop others from writing on the opened file
                thread_current()->fdt[i]=openedFile; //save the file pointer to the fdt
                rtrn = i;
                break;
            }
        }
    }
    lock_release(&filesys_lock);
    return rtrn;
}

int filesize(int fd){
    //check if fd is valid
    if(thread_current() -> fdt[fd]==NULL)
        exit(-1);
    //chage file_discriptor=> fdt
    return (int)file_length(thread_current()->fdt[fd]);
}

int read(int fd, void *buffer, unsigned size){
    int i=-1;
    check_pointer(buffer);
    //keep file locked from other processes
    lock_acquire(&filesys_lock);
    //if fd is 0 (cin)
    if(fd==0){
        for(i=0;i<size; i++){
            if(input_getc()=='\0')
                break; //break if end of file
        }
    } else if(fd>2){
        //if fd is null exit
        //change -> fdt
        if(thread_current()->fdt[fd] == NULL){
            lock_release(&filesys_lock);
            exit(-1);
        }
        //read file
        i=file_read(thread_current()->fdt[fd],buffer,size);
    }
    lock_release(&filesys_lock);
    return i;
}

int write(int fd, const void *buffer, unsigned size){
    check_pointer(buffer);
    struct thread *cur = thread_current();
    lock_acquire(&filesys_lock);
    int rtrn=-1;
    if(fd==1){
        putbuf(buffer,size); //putbuf in console.c for cout
        rtrn=size;
    } else if (fd >2){
        if(cur->fdt[fd]==NULL){
            lock_release(&filesys_lock);
            exit(-1);
        }
	//if(cur->fdt[fd]->inode==NULL)
	//	cur->fdt[fd]->deny_write=false;
        /*if(cur->fdt[fd]->deny_write){
            file_deny_write(cur->fdt[fd]);
        }*/
        rtrn=file_write(cur->fdt[fd], buffer, size);
    }
    lock_release(&filesys_lock);
    return rtrn;
}

void seek(int fd, unsigned position){
    //check if fd is valid
    if(thread_current() -> fdt[fd]==NULL)
        exit(-1);
    file_seek(thread_current()->fdt[fd],position);
}

unsigned tell(int fd){
    //check if fd is valid
    if(thread_current() -> fdt[fd]==NULL)
        exit(-1);
    return (unsigned)file_tell(thread_current()->fdt[fd]);
}

void close(int fd){
    //check if the fd is valid
    if(thread_current() -> fdt[fd]==NULL)
        exit(-1);
    //close
    file_close(thread_current()->fdt[fd]);
	// set to NULL
	thread_current()->fdt[fd]=NULL;
}

