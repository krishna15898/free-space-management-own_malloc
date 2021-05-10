#include <stdio.h>
#include <sys/mman.h>
#include <errno.h>
#include <err.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

//defining constants
#define BLOCK_SIZE 4096
#define MAGIC_NUMBER 1234567

// defining the data structure for freelist node
typedef struct _node {//size=16
	int size;
	struct _node *next;
} node;

// defining the data structure for allocated nodes
typedef struct _header {//size=8
	int size;
	int magic;
} header;

//defining initial variables as given on piazza
int *a,*b,*c,*d,*e,*f;
node *head;//head of the free list.
int *base_address;

// defining functions
int my_init();
void* my_alloc(int size);
void my_free(void* address);
void my_clean();
void my_heapinfo();
node *find_free(int size);
void print_free_list();//own function for debugging
void push_new_node(node *n);//for coalescing
void merge_nodes(node *n);//merges n1 and n1->next
void print_both(node *n1);//own function
void coalesce();//function fo coalescing free nodes
void update_e_and_f();//calc *e, *f
void remove_node(node *n);//remove a free node from list

int my_init(){
	if ((head = mmap (NULL, BLOCK_SIZE, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0))==MAP_FAILED) {
	 	int err_alloc = errno;
		perror("allocation error:");
		return err_alloc;
	}
	//initialising first header block for the whole space
	head->size=BLOCK_SIZE-sizeof(node)-6*sizeof(int);
	head->next=NULL;

	base_address=(int*)head;
	//allocating space for heap info (inside the 4KB)
	//keeping these vales at the end of the space
	f=(int*)head+(BLOCK_SIZE-4)/4;
	e=f-1;
	d=e-1;
	c=d-1;
	b=c-1;
	a=b-1;

	//initialising heap info values
	(*a)=4096-6*sizeof(int);//maximum size
	(*b)=sizeof(node);//current size
	(*c)=(*a)-(*b);//free space
	(*d)=0;//zero blocks are occupied right now
	(*e)=(*c);//smallest free slot is the free space
	(*f)=(*c);//same as smallest free slot
	return 0;
}

void* my_alloc(int size) {
	if (!size || (size%8)!=0){
		return NULL;
	}
	int tot_size=size+sizeof(header);
	node *free_node=find_free(tot_size);
	if (!free_node) return NULL;
	
	//handling case where the left free space is 8
	if ((free_node->size+sizeof(node)-tot_size)==8) {
		tot_size+=8;
		size+=8;
		(*b)-=sizeof(node);
		remove_node(free_node);
	}
	// if left space is 0 then remove the node from list
	else if ((free_node->size+sizeof(node)-tot_size)==0) {
		remove_node(free_node);
		(*b)-=sizeof(node);
	}	
	else {
		//making new node for free list
		node *n=(node*)((int*)free_node+tot_size/4);
		n->next=free_node->next;
		n->size=free_node->size-tot_size;
		push_new_node(n);
		remove_node(free_node);
	}
	//updating heap info
	(*b)+=tot_size;
	(*c)=(*a)-(*b);
	(*d)++;//increasing the number of blocks
	
	//making a new header node for allocated memory
	header *block=(header *)free_node;
	block->size=size;
	block->magic=MAGIC_NUMBER;
	
	update_e_and_f();//updating heap info
	memset(block+1,0,size);//setting initial  values to zero
	return (void*)(block+1);
}

void remove_node(node *n) {
	node *curr=head;
	if (head==n) {
		head=n->next;
	}
	while (curr->next) {
		if (curr->next==n) 
			curr->next=curr->next->next;
		curr=curr->next;
	}
}

// first fit strategy used to find a free slot. 
node *find_free(int tot_size) {
	node *curr = head;
	while (curr) {
		if (curr->size+sizeof(node)>=tot_size) {
			return curr;
		}
		curr=curr->next;
	}
	return NULL;
}

void my_clean() {
	if (munmap(base_address,BLOCK_SIZE)==-1) {
		int err_unmap=errno;
		perror("cleaning error:");
	}
}

void my_free(void *address) {
	if (!address) return;//if address null, do nothing

	header *h=(header *)((int*)address-(sizeof(header)/4));
	//sanity check
	if (h->magic!=MAGIC_NUMBER) return;

	int tot_size=h->size+sizeof(header);
	
	//making a free list node in the freed space
	node *new_free;
	new_free=(node*)((int*)address-(sizeof(header)/4));//node has size 8 bytes
	new_free->size=tot_size-sizeof(node);
	new_free->next=NULL;

	//updating heapinfo
	(*b)=(*b)-tot_size+sizeof(node);
	(*c)=(*a)-(*b);
	(*d)--;
	
	//rearranging and coalescing the freelist 
	push_new_node(new_free);
	coalesce();
	update_e_and_f();//updating heap info
	return;
}

void push_new_node(node *n) { 
	if (!head) return;

	if (head>n) { 
		n->next=head;
		head=n;
		return;
	} 
	node *curr = head;
	while (curr->next) {
		if (curr->next>n) {
			n->next=curr->next;
			curr->next=n;
			return;
		} 
		curr=curr->next;
	} 
	curr->next=n;
	n->next=NULL;
	return;
} 

void print_both(node *n) {
	printf("%p %p\n",n,n->next );
	printf("size of node on the left=%d\n",n->size);
	printf("size of node on the right=%d\n",n->next->size);
	printf("sizeof(node)=%ld\n", sizeof(node));
	printf("%p %p\n",(int*)n+(n->size+sizeof(node))/4,n->next );
}

void coalesce() {
	node* curr=head;
	while (curr) {
		if (((int*)curr+(sizeof(node)+curr->size)/4)==((int*)curr->next)) {
			merge_nodes(curr);
			continue;
		}
		curr=curr->next;
	}
}

void merge_nodes(node *n) {
	if (!n || !(n->next)) return;
	n->size=n->size+sizeof(node)+n->next->size;
	n->next=n->next->next;
	(*b)=(*b)-sizeof(node);
	(*c)=(*a)-(*b);
	update_e_and_f();
	return;
}

void my_heapinfo() {
	// a=max_size;
	// b=curr_size;
	// c=free_mem;
	// d=num_blocks;
	// e=smallest;
	// f=largest;
	// Do not edit below output format
	printf("=== Heap Info ================\n");
	printf("Max Size: %d\n", *a);
	printf("Current Size: %d\n", *b);
	printf("Free Memory: %d\n", *c);
	printf("Blocks allocated: %d\n", *d);
	printf("Smallest available chunk: %d\n", *e);
	printf("Largest available chunk: %d\n", *f);
	printf("==============================\n");
	// Do not edit above output format
	return;
}

void print_free_list() {
	int count=1;
	node *curr=head;
	printf("count\taddress\t\tsize\tnext\n");
	while (curr) {
		printf("%d\t%p\t%d\t%p\n", count,curr,curr->size,curr->next);
		curr=curr->next;
		count++;
	}
}

void update_e_and_f() {
	node *curr=head;
	int small=(*a);
	int large=0;
	int found_zero=0;
	while (curr) {
		if (curr->size>large)
			large=curr->size;
		if (curr->size<small){
			if (curr->size<=0) 
				found_zero=1;
			else
				small=curr->size;
		}
		curr=curr->next;
	}
	if (found_zero && small!=(*a)) 
		(*e)=0;
	else
		(*e)=small;
	(*f)=large;
}
