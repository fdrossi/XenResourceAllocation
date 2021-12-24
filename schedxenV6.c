
/**
 * pra compilar:
 * gcc `xml2-config --cflags --libs` -o schedv3 schedv3.c /root/xen-3.0.4_1-src/tools/xenstat/libxenstat/src/libxenstat.a
 * pra teste:
 * ./sched <nome_do_arquivo>.xml
 */

//XML includes xml
#include <string.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
//end includes

#include <curses.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <linux/kdev_t.h>
#include <sys/types.h>
#include <unistd.h>
#include "/home/xen/Desktop/xencpu/xenstat.h"

//Limit rate of processing, each domain
#define LIMIT_WEIGHT 95 

//---------------------------------------------------------------------------------//
//                      Global Variables                                           //
//---------------------------------------------------------------------------------//

int error = 1;
struct timeval curtime, oldtime;
xenstat_handle *xhandle = NULL;
xenstat_node *prev_node = NULL;
xenstat_node *cur_node = NULL;
unsigned int first_domain_index = 1;
unsigned int delay = 15, realtime=0;
int capfound=0;
int found_exec=0;
int scal_true=0;

//variables of libxml
xmlNodePtr root; //XML Node
char caps[4][3];
char mems[4][3];
char cap_original[4][3]; //to compare slice of CPU (cap)
char mem_original[4][3]; //to compare memory
char compc[4][8]; //compare cap string
char compm[4][8]; //compare memory string
char compC[6]; //compare process load string
char exec[10]; //execution number of tree xml
char carga[6]; //process load of execution
char otim[10][4]; //otimization array
int cont_otim=0; //counting of array otimization
//end

//---------------------------------------------------------------------------------//
//                      Function Heads                                             //
//---------------------------------------------------------------------------------//
static void getXMLProperties(xmlNode * a_node);
static void getXMLExec(xmlNode * a_node);
static void getNextXMLExec(xmlNode * a_node);
static void LoadXML(const char *filename);
static double get_pct_cpu(xenstat_domain *domain);
static int get_vcpu_online(xenstat_domain *domain);
static int get_cap (xenstat_domain *domain);
static void print_information(xenstat_domain *domain, int cap, int mem, double pct);
static void sched_cap (int valor, xenstat_domain *domain);
static void sched_mem (int valor, xenstat_domain *domain);
static void start();

//---------------------------------------------------------------------------------//
//                             Load xml                                            //
//---------------------------------------------------------------------------------//
static void LoadXML(const char *filename)
{
    xmlDocPtr doc;
    
	
    doc = xmlReadFile(filename, NULL, 0);
    if (doc == NULL) {
        fprintf(stderr, "Parser Failed to %s\n", filename);
	return;
    }
    printf("Loading Xml ...\n"); 	
    root = xmlDocGetRootElement(doc);

    //xmlFreeDoc(doc);
}
//---------------------------------------------------------------------------------//
//                Get Properties of node execution on the tree xml                 //
//---------------------------------------------------------------------------------//
static void getXMLProperties(xmlNode * a_node)
{
int i=0, cont=0, cont2=0;
xmlNode *cur_node = NULL;
xmlNode *prev_node = NULL;

for (cur_node = a_node; cur_node; cur_node = cur_node->next) {
	
	if (!xmlStrcmp(cur_node->name, "nodo")) { 
		if (!xmlStrcmp(cur_node->properties->children->content, exec)){

		for (prev_node = cur_node->children; prev_node; prev_node = prev_node->next) {
			if(!xmlStrcmp(prev_node->name, compC)) {
            			strcpy(carga, prev_node->children->content);
				
        		}
			
			for (i=0; i<4; i++){
				
        			if(!xmlStrcmp(prev_node->name, compm[i])) {
            				strcpy(mems[i], prev_node->children->content);
				
        			}
		
				if(!xmlStrcmp(prev_node->name, compc[i])) {
					strcpy(caps[i], prev_node->children->content);
				
		
				}
			}
		}
	}}
        getXMLProperties(cur_node->children);
	
 }
}
//---------------------------------------------------------------------------------//
//                Search for the next node execution on the tree xml               //
//---------------------------------------------------------------------------------//
static void getNextXMLExec(xmlNode * a_node)
{
int i=0, cont=0, cont2=0;
xmlNode *cur_node = NULL;
xmlNode *prev_node = NULL;

for (cur_node = a_node; cur_node; cur_node = cur_node->next) {
	
	if (!xmlStrcmp(cur_node->name, "nodo")) { 
		if (!xmlStrcmp(cur_node->properties->children->content, exec)){

		for (prev_node = cur_node->children; prev_node; prev_node = prev_node->next){
			if (!xmlStrcmp(prev_node->name, "otimizacao")){
				strcpy(otim[cont_otim], prev_node->children->content);
				cont_otim++;
			}}
						
	}}
       getNextXMLExec(cur_node->children);
	
 }
}
//---------------------------------------------------------------------------------//
//          Verify cap e memory, and get number of execution on the tree xml       //
//---------------------------------------------------------------------------------//
static void getXMLExec(xmlNode * a_node)
{

int i=0, cont=0, cont2=0;
xmlNode *cur_node = NULL;
xmlNode *prev_node = NULL;
char carga2[6];


for (cur_node = a_node; cur_node; cur_node = cur_node->next) {

	if(!xmlStrcmp(cur_node->name, "carga"))
		strcpy(carga2,cur_node->children->content);

	for (i=0; i<4; i++){

		if((!xmlStrcmp(cur_node->name, compc[i])) || (!xmlStrcmp(cur_node->name, compm[i]))) {
			strcpy(caps[i], cur_node->children->content);
			strcpy(mems[i], cur_node->children->content);
			if (!strcmp(caps[i], cap_original[i+1])) {
				cont++;
			}
			if (!strcmp(mems[i], mem_original[i+1])) {
				cont2++;
			}
			
			if (cont==4 && cont2==4)   {

				strcpy(exec,cur_node->parent->properties->children->content);
				strcpy(carga,carga2);
				

			}
		}
	}
        getXMLExec(cur_node->children);
}
}
//---------------------------------------------------------------------------------//
//       Computes the CPU percentage used for a specified domain                   //
//---------------------------------------------------------------------------------//
static double get_pct_cpu(xenstat_domain *domain)
{
        xenstat_domain *old_domain;
	    double us_elapsed;

         //Can't calculate CPU percentage without a previous sample
         if(prev_node == NULL)
                 return 0.0 ;

        old_domain = xenstat_node_domain(prev_node, xenstat_domain_id(domain));
        if(old_domain == NULL)
		 return 0.0;

         // Calculate the time elapsed in microseconds 
         us_elapsed = ((curtime.tv_sec-oldtime.tv_sec)*1000000.0
                       +(curtime.tv_usec - oldtime.tv_usec));

		 // In the following, nanoseconds must be multiplied by 1000.0 to
         // convert to microseconds, then divided by 100.0 to get a percentage,
         // resulting in a multiplication by 10.0
         return (xenstat_domain_cpu_ns(domain)-xenstat_domain_cpu_ns(old_domain))/10.0/us_elapsed;
}

//---------------------------------------------------------------------------------//
//           Returns VCPUS online of each domain                                   //
//---------------------------------------------------------------------------------//
static int get_vcpu_online(xenstat_domain *domain) {

    int i = 0;
	int cont = 0;
	unsigned num_vcpus = 0;
	xenstat_vcpu *vcpu;
	num_vcpus = xenstat_domain_num_vcpus(domain);

	//look all vcpus and count the atives
	for (i=0; i< num_vcpus; i++) {
		vcpu = xenstat_domain_vcpu(domain,i);
		if (xenstat_vcpu_online(vcpu) > 0) {
			cont++;
		}
	}
	return cont;

}

//---------------------------------------------------------------------------------//
//                   Memory Scheduling                                             //
//---------------------------------------------------------------------------------//
static void sched_mem (int valor_mem, xenstat_domain *domain)
{

    int pid;
    char valormem[10];
    sprintf(valormem, "%d", valor_mem);

	//two process
	pid = fork();
    
	if (pid == 0)
	{
        printf("executou sched_mem\n");
	}
	else
	{

		execl("/usr/sbin/xm","/usr/sbin/xm", "mem-set",xenstat_domain_name(domain),valormem,NULL);

	}


}


//---------------------------------------------------------------------------------//
//                 Cap Scheduling                                                  //
//---------------------------------------------------------------------------------//
static void sched_cap (int valor, xenstat_domain *domain)
{

    int pid;
    char valorcap[5];
    sprintf(valorcap, "%d", valor);

	//two process 
	pid = fork();

        
	if (pid == 0)
	{
        printf("executou sched_cap\n");
	}
	else
	{

		execl("/usr/sbin/xm","/usr/sbin/xm", "sched-credit","-d",xenstat_domain_name(domain),"-c",valorcap,NULL);

	}




}


//---------------------------------------------------------------------------------//
//                 Retuns CAP of each domain                                       //
//---------------------------------------------------------------------------------//
static int get_cap (xenstat_domain *domain)
{


	int	fds[2];
	int	pid;
	char	buf[50];
	char delims[]=": ";
	char *result=NULL;
        int cap;

	//Create Pipe
	if (pipe(fds) != 0)
	{
		printf("Erro criando pipe\n");
		return(1);
	}

	//two process
	pid = fork();

	if (pid == 0)
	{
		memset(buf, 0, sizeof(buf));
		read(fds[0], buf, sizeof(buf));

		//Capture cap, and convert string to int
		result = strtok(buf,delims);
		char delims[]= ",";
		result = strtok(NULL,delims);
		cap=atoi(result);
	}
	else
	{

		close(1);
		dup2(fds[1], 1);
        //System call "xm" 
		execl("/usr/sbin/xm","/usr/sbin/xm", "sched-credit","-d",xenstat_domain_name(domain),NULL);

	}

        return cap;

}


//---------------------------------------------------------------------------------//
//                    Print Information about Online Domains                       //
//---------------------------------------------------------------------------------//
static void print_information(xenstat_domain *domain, int cap, int mem, double pct)
{


		printf("Domains informations\n");
		printf("N EXEC		%s\n",exec);
		printf("NAME		%s\n",xenstat_domain_name(domain));
		printf("CPU(%)	     %6.1f\n", get_pct_cpu(domain));
		printf("CAP		%d\n",cap);
		printf("PCT PER CAP  %6.1f\n",pct);
		printf("VCPUs ON	%d\n",get_vcpu_online(domain));
		printf("VCPUs TOTAL	%d\n",xenstat_domain_num_vcpus(domain));
		printf("MEM TOTAL	%d\n",mem);

}
//---------------------------------------------------------------------------------//
//               Starts Domains                                                    //
//---------------------------------------------------------------------------------//
static void start(const char *filename)
{
    xenstat_domain **domains, *domains_ant;
    unsigned int i,num_domains = 0, num_domains_ant;
    double pct; //percentage of CPU
    int j, cap_domain=-1, mem_domain=-1;
    int num=0;	
    

	//Now get the node information
	if (prev_node != NULL)
		xenstat_free_node(prev_node);


	prev_node = cur_node;
	cur_node = xenstat_get_node(xhandle, XENSTAT_ALL);

	if (cur_node == NULL)
		printf("Failed to retrieve statistics from libxenstat\n");

	//Count the number of domains for which to report data
	num_domains = xenstat_node_num_domains(cur_node);
	domains = malloc(num_domains*sizeof(xenstat_domain *));

	
	if(domains == NULL)
		printf("Failed to allocate memory\n");


	for (i=0; i < num_domains; i++)
		domains[i] = xenstat_node_domain_by_index(cur_node, i);


	if(first_domain_index > num_domains)
		first_domain_index = num_domains-1;




	//--------------- NOTES AS PERCENTAGE FOR SCHEDULING -----------------------------------//

	for (i = first_domain_index; i < num_domains; i++)
	{


		if(capfound < 4) {
			sprintf(cap_original[i],"%d",get_cap(domains[i]));
			sprintf(mem_original[i],"%u",xenstat_domain_cur_mem(domains[i])/1048576);
			//printf("CAP %d: %s\n",i, cap_original[i]);
			//printf("MEM %d: %s\n\n",i, mem_original[i]);
			capfound++;
		}
	 
		if((capfound >= 4) && (found_exec == 0))
		{
			LoadXML(filename);
			getXMLExec(root);
			found_exec=1;
		}
		if (found_exec ==1 )
		{
			getXMLProperties(root);
			//printf("Execução: %s\n", exec);
			found_exec=2;
		}

		if(found_exec >= 2) {

			//tranform string to int
			cap_domain=atoi(caps[i-1]); 
			mem_domain=atoi(mems[i-1]); 

			//verify percentage to 1 CPU
			pct=get_pct_cpu(domains[i])*100/cap_domain; 

			//Print Information about Currents Domains
			print_information(domains[i],cap_domain,mem_domain,pct); 
			
		   	   //If one VM have more rate of processing than the limit
			   //so realocate resource of VM (cap and memory)
		   	   if (pct >= LIMIT_WEIGHT) {
				   cont_otim=0;
				   getNextXMLExec(root);
				   if(cont_otim > 0) {	
				   	srand(time(NULL));
				   	num=rand() % cont_otim;
				   	strcpy(exec,otim[num]);
				   	getXMLProperties(root);	
				   
				   	for (j = first_domain_index; j < num_domains; j++) {
						cap_domain=atoi(caps[j-1]);
						mem_domain=atoi(mems[j-1]);
						sched_cap(cap_domain,domains[j]);
						sched_mem(mem_domain,domains[j]);
				   	}
				   scal_true=1;
				   i = num_domains;
				  }
	           	    }
		}
	}
	
	
    //If realocate, stop for 15 seconds
    if(scal_true==1) {
       printf("SLEEPING ...\n");	
       sleep(15);
       capfound=0;
       scal_true=0;	
    }




}

//---------------------------------------------------------------------------------//
//                        Main                                                     //
//---------------------------------------------------------------------------------//
int main(int argc, char **argv)
 {
        	 
	 int i;

	 for (i=0;i<4;i++) sprintf(compc[i], "vm0%dcap", i+1);
	 for (i=0;i<4;i++) sprintf(compm[i], "vm0%dmem", i+1);

	//begins libxml2
	LIBXML_TEST_VERSION

        //begins libxenstat
        xhandle = xenstat_init();

        if (xhandle == NULL)
            printf("Failed to initialize xenstat library\n");

        do {
            gettimeofday(&curtime, NULL);
			if(error != 0 || (curtime.tv_sec - oldtime.tv_sec) >= delay) {
				 start(argv[1]);
				 oldtime = curtime;
				 sleep(3);
	           	}

		} while (error);
}


