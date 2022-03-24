 /*
  * host.c
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>


#include <unistd.h>
#include <fcntl.h>


#include "main.h"
#include "net.h"
#include "man.h"
#include "host.h"
#include "packet.h"


#define MAX_FILE_BUFFER 1000
#define MAX_MSG_LENGTH 100
#define MAX_DIR_NAME 100
#define MAX_FILE_NAME 100
#define PKT_PAYLOAD_MAX 100
#define TENMILLISEC 10000   /* 10 millisecond sleep */




/* Types of packets */


struct file_buf {
	char name[MAX_FILE_NAME];
	int name_length;
	char buffer[MAX_FILE_BUFFER+1];
	int head;
	int tail;
	int occ;
	FILE *fd;
};


char * dirarray[100];


/*
 * File buffer operations
 */


/* Initialize file buffer data structure */
void file_buf_init(struct file_buf *f)
{
f->head = 0;
f->tail = MAX_FILE_BUFFER;
f->occ = 0;
f->name_length = 0;
}


/* 
 * Get the file name in the file buffer and store it in name 
 * Terminate the string in name with tne null character
 */
void file_buf_get_name(struct file_buf *f, char name[])
{
int i;


for (i=0; i<f->name_length; i++) {
	name[i] = f->name[i];
}
name[f->name_length] = '\0';
}


/*
 *  Put name[] into the file name in the file buffer
 *  length = the length of name[]
 */
void file_buf_put_name(struct file_buf *f, char name[], int length)
{
int i;


for (i=0; i<length; i++) {
	f->name[i] = name[i];
}
f->name_length = length;
}


/*
 *  Add 'length' bytes n string[] to the file buffer
 */
int file_buf_add(struct file_buf *f, char string[], int length)
{
int i = 0;


while (i < length && f->occ < MAX_FILE_BUFFER) {
	f->tail = (f->tail + 1) % (MAX_FILE_BUFFER + 1);
	f->buffer[f->tail] = string[i];
	i++;
        f->occ++;
}
return(i);
}


/*
 *  Remove bytes from the file buffer and store it in string[] 
 *  The number of bytes is length.
 */
int file_buf_remove(struct file_buf *f, char string[], int length)
{
int i = 0;




while (i < length && f->occ > 0){
	string[i] = f->buffer[f->head];
	f->head = (f->head + 1) % (MAX_FILE_BUFFER + 1);
	i++;
        f->occ--;
}


return(i);
}




/*
 * Operations with the manager
 */


int get_man_command(struct man_port_at_host *port, char msg[], char *c) {


int n;
int i;
int k;


n = read(port->recv_fd, msg, MAN_MSG_LENGTH); /* Get command from manager */
if (n>0) {  /* Remove the first char from "msg" */
	for (i=0; msg[i]==' ' && i<n; i++);
	*c = msg[i];
	i++;
	for (; msg[i]==' ' && i<n; i++);
	for (k=0; k+i<n; k++) {
		msg[k] = msg[k+i];
	}
	msg[k] = '\0';
}
return n;


}


/*
 * Operations requested by the manager
 */


/* Send back state of the host to the manager as a text message */
void reply_display_host_state(
		struct man_port_at_host *port,
		char dir[],
		int dir_valid,
		int host_id)
{
int n;
char reply_msg[MAX_MSG_LENGTH];


if (dir_valid == 1) {
	n =sprintf(reply_msg, "%s %d", dir, host_id);
}
else {
	n = sprintf(reply_msg, "None %d", host_id);
}


write(port->send_fd, reply_msg, n);
}






/* Job queue operations */


/* Add a job to the job queue */
void job_q_add(struct job_queue *j_q, struct host_job *j)
{
if (j_q->head == NULL ) {
	j_q->head = j;
	j_q->tail = j;
	j_q->occ = 1;
}
else {
	(j_q->tail)->next = j;
	j->next = NULL;
	j_q->tail = j;
	j_q->occ++;
}
}


/* Remove job from the job queue, and return pointer to the job*/
struct host_job *job_q_remove(struct job_queue *j_q)
{
struct host_job *j;


if (j_q->occ == 0) return(NULL);
j = j_q->head;
j_q->head = (j_q->head)->next;
j_q->occ--;
return(j);
}


/* Initialize job queue */
void job_q_init(struct job_queue *j_q)
{
j_q->occ = 0;
j_q->head = NULL;
j_q->tail = NULL;
}


int job_q_num(struct job_queue *j_q)
{
return j_q->occ;
}


/*
 *  Main 
 */


void host_main(int host_id)
{


/* State */
//char * dirarray[100];
char dir[MAX_DIR_NAME];
int dir_valid = 0;


char man_msg[MAN_MSG_LENGTH];
char man_reply_msg[MAN_MSG_LENGTH];
char man_cmd;
struct man_port_at_host *man_port;  // Port to the manager


struct net_port *node_port_list;
struct net_port **node_port;  // Array of pointers to node ports
int node_port_num;            // Number of node ports


int ping_reply_received;
int testdir = 0;
int i, k, n;
int dst;
int src;
char name[MAX_FILE_NAME];
char string[PKT_PAYLOAD_MAX+1]; 


FILE *fp;


struct packet *in_packet; /* Incoming packet */
struct packet *new_packet;


struct net_port *p;
struct host_job *new_job;
struct host_job *new_job2;


struct job_queue job_q;


struct file_buf f_buf_upload;  
struct file_buf f_buf_download; 


file_buf_init(&f_buf_upload);
file_buf_init(&f_buf_download);


/*
 * Initialize pipes 
 * Get link port to the manager
 */


man_port = net_get_host_port(host_id);


/*
 * Create an array node_port[ ] to store the network link ports
 * at the host.  The number of ports is node_port_num
 */
node_port_list = net_get_port_list(host_id); 


	/*  Count the number of network link ports */
node_port_num = 0;
for (p=node_port_list; p!=NULL; p=p->next) {
	node_port_num++;
}
	/* Create memory space for the array */
node_port = (struct net_port **) 
	malloc(node_port_num*sizeof(struct net_port *));


	/* Load ports into the array */
p = node_port_list;
for (k = 0; k < node_port_num; k++) {
	node_port[k] = p;
	p = p->next;
}	


/* Initialize the job queue */
job_q_init(&job_q);


//NEW LOOP?


while(1) {
	/* Execute command from manager, if any */


		/* Get command from manager */
	n = get_man_command(man_port, man_msg, &man_cmd);


		/* Execute command */
	if (n>0) {
		switch(man_cmd) {
			case 's':
				reply_display_host_state(man_port,
					dir, 
					dir_valid,
					host_id);
				break;	
			
			case 'm':
				dir_valid = 1;
            for (i=0; man_msg[i] != '\0'; i++) {
					dir[i] = man_msg[i];
				}
				dir[i] = man_msg[i];


            dirarray[host_id] = dir;


            break;


			case 'p': // Sending ping request
				// Create new ping request packet
				sscanf(man_msg, "%d", &dst);
				new_packet = (struct packet *) 
						malloc(sizeof(struct packet));	
				new_packet->src = (char) host_id;
				new_packet->dst = (char) dst;
				new_packet->type = (char) PKT_PING_REQ;
				new_packet->length = 0;
				new_job = (struct host_job *) 
						malloc(sizeof(struct host_job));
				new_job->packet = new_packet;
				new_job->type = JOB_SEND_PKT_ALL_PORTS;
				job_q_add(&job_q, new_job);


				new_job2 = (struct host_job *) 
						malloc(sizeof(struct host_job));
				ping_reply_received = 0;
				new_job2->type = JOB_PING_WAIT_FOR_REPLY;
				new_job2->ping_timer = 10;
				job_q_add(&job_q, new_job2);


				break;


			case 'u': /* Upload a file to a host */
            //printf("CHECKPOINT A0\n");
				//printf("YO THIS IS DIR %s\n",dir);
            sscanf(man_msg, "%d %s", &dst, name);
				new_job = (struct host_job *) 
						malloc(sizeof(struct host_job));
				new_job->type = JOB_FILE_UPLOAD_SEND;
				new_job->file_upload_dst = dst;	
				for (i=0; name[i] != '\0'; i++) {
					new_job->fname_upload[i] = name[i];
				}
				new_job->fname_upload[i] = '\0';
				job_q_add(&job_q, new_job);
            //printf("YO THIS IS DIR 2 %s\n",dir);


				//printf("CHECKPOINT A1\n");
				break;


            //Check if something needs to be added? or deleted?
         case 'd': /* Download a file */
          //  printf("CHECKPOINT 0\n");
            sscanf(man_msg, "%d %s", &src, name); //Scan for host name?
      
            //Create a Packet to send
            new_packet = (struct packet *)
               malloc(sizeof(struct packet));
            for(i=0; name[i] !='\0'; i++){
               new_packet->payload[i] = name[i];
            }
            new_packet->length = i;
            new_packet->src = (char) host_id; //Send packet from first host
            new_packet->dst = src; //To second host (Source of file)
            new_packet->type = (char) PKT_FILE_DOWNLOAD_PING;


            new_job = (struct host_job *)
               malloc(sizeof(struct host_job));
            new_job->type = JOB_SEND_PKT_ALL_PORTS;
            new_job->file_download_src = src;
            new_job->packet = new_packet;
            job_q_add(&job_q, new_job);


            


               




        //    printf("CHECKPOINT 1\n");


            break;


            //ADDED DOWNLOAD JOB?
            //THe job will use the packet upload
			default:
			;
		}
	}
	
	/*
	 * Get packets from incoming links and translate to jobs
  	 * Put jobs in job queue
 	 */


	for (k = 0; k < node_port_num; k++) { /* Scan all ports */


		in_packet = (struct packet *) malloc(sizeof(struct packet));
		n = packet_recv(node_port[k], in_packet);






		if ((n > 0) && ((int) in_packet->dst == host_id)) {
			new_job = (struct host_job *) 
				malloc(sizeof(struct host_job));
			new_job->in_port_index = k;
			new_job->packet = in_packet;


         //in_packet->src == host_id && n>0
         //
			switch(in_packet->type) {
				/* Consider the packet type */


				/* 
				 * The next two packet types are 
				 * the ping request and ping reply
				 */
				case (char) PKT_PING_REQ: 
					new_job->type = JOB_PING_SEND_REPLY;
					job_q_add(&job_q, new_job);
					break;


				case (char) PKT_PING_REPLY:
					ping_reply_received = 1;
					free(in_packet);
					free(new_job);
					break;


				/* 
				 * The next two packet types
				 * are for the upload file operation.
				 *
				 * The first type is the start packet
				 * which includes the file name in
				 * the payload.
				 *
				 * The second type is the end packet
				 * which carries the content of the file
				 * in its payload
				 */


				case (char) PKT_FILE_UPLOAD_START:
					new_job->type 
						= JOB_FILE_UPLOAD_RECV_START;
					job_q_add(&job_q, new_job);
					break;


				case (char) PKT_FILE_UPLOAD_END:
					new_job->type 
						= JOB_FILE_UPLOAD_RECV_END;
					job_q_add(&job_q, new_job);
					break;
				
            /*
             *Two packet types
             *Download file operation
             */
               
            case (char) PKT_FILE_DOWNLOAD_PING:
               //USed to check for dir 
               //Take name, src, length from case 'd'
               new_job->type = JOB_FILE_DOWNLOAD_SEND; 
               for(i=0; i < in_packet->length; ++i){
                  new_job->fname_download[i] = in_packet->payload[i];}
               new_job->fname_download[i] = '\0';
              // printf("new_job fn ame = %s\n",new_job->fname_download);
               new_job->file_download_src = in_packet->src;
               job_q_add(&job_q, new_job);
               free(in_packet);
               break;


            case (char) PKT_FILE_DOWNLOAD_START:
               new_job->type
                  = JOB_FILE_DOWNLOAD_RECV_START;
               job_q_add(&job_q, new_job);
               break;


            case (char) PKT_FILE_DOWNLOAD_END:
               new_job->type
                  = JOB_FILE_DOWNLOAD_RECV_END;
               job_q_add(&job_q, new_job);
               break;




            default:
					free(in_packet);
					free(new_job);
			}
		}
		else {
			free(in_packet);
		}
	}


	/*
 	 * Execute one job in the job queue
 	 */


	if (job_q_num(&job_q) > 0) {


		/* Get a new job from the job queue */
		new_job = job_q_remove(&job_q);




		/* Send packet on all ports */
		switch(new_job->type) {


		/* Send packets on all ports */	
		case JOB_SEND_PKT_ALL_PORTS:
			for (k=0; k<node_port_num; k++) {
				packet_send(node_port[k], new_job->packet);
			}
			free(new_job->packet);
			free(new_job);
			break;


		/* The next three jobs deal with the pinging process */
		case JOB_PING_SEND_REPLY:
			/* Send a ping reply packet */


			/* Create ping reply packet */
			new_packet = (struct packet *) 
				malloc(sizeof(struct packet));
			new_packet->dst = new_job->packet->src;
			new_packet->src = (char) host_id;
			new_packet->type = PKT_PING_REPLY;
			new_packet->length = 0;


			/* Create job for the ping reply */
			new_job2 = (struct host_job *)
				malloc(sizeof(struct host_job));
			new_job2->type = JOB_SEND_PKT_ALL_PORTS;
			new_job2->packet = new_packet;


			/* Enter job in the job queue */
			job_q_add(&job_q, new_job2);


			/* Free old packet and job memory space */
			free(new_job->packet);
			free(new_job);
			break;


		case JOB_PING_WAIT_FOR_REPLY:
			/* Wait for a ping reply packet */


			if (ping_reply_received == 1) {
				n = sprintf(man_reply_msg, "Ping acked!"); 
				man_reply_msg[n] = '\0';
				write(man_port->send_fd, man_reply_msg, n+1);
				free(new_job);
			}
			else if (new_job->ping_timer > 1) {
				new_job->ping_timer--;
				job_q_add(&job_q, new_job);
			}
			else { /* Time out */
				n = sprintf(man_reply_msg, "Ping time out!"); 
				man_reply_msg[n] = '\0';
				write(man_port->send_fd, man_reply_msg, n+1);
				free(new_job);
			}


			break;	




		/* The next three jobs deal with uploading a file */


			/* This job is for the sending host */
		case JOB_FILE_UPLOAD_SEND:


			/* Open file */
			if (dir_valid == 1) {
				n = sprintf(name, "./%s/%s", 
					dir, new_job->fname_upload);
				name[n] = '\0';
            //printf("CHECK NAME A = %s\n",name);
				fp = fopen(name, "r");
				if (fp != NULL) {
               
				        /* 
					 * Create first packet which
					 * has the file name 
					 */
					new_packet = (struct packet *) 
						malloc(sizeof(struct packet));
					new_packet->dst 
						= new_job->file_upload_dst;
					new_packet->src = (char) host_id;
					new_packet->type 
						= PKT_FILE_UPLOAD_START;
					for (i=0; 
						new_job->fname_upload[i]!= '\0'; 
						i++) {
						new_packet->payload[i] = 
							new_job->fname_upload[i];
					}
					new_packet->length = i;


					/* 
					 * Create a job to send the packet
					 * and put it in the job queue
					 */
					new_job2 = (struct host_job *)
						malloc(sizeof(struct host_job));
					new_job2->type = JOB_SEND_PKT_ALL_PORTS;
					new_job2->packet = new_packet;
					job_q_add(&job_q, new_job2);


					/* 
					 * Create the second packet which
					 * has the file contents
					 */
				


             //  new_packet = (struct packet *) 
				//		malloc(sizeof(struct packet));
/*               
               new_packet->dst
                  = new_job->file_upload_dst;
               new_packet->src = (char) host_id;
               new_packet->type = PKT_FILE_UPLOAD_END;
*/
               //Loop start?
               
					n = fread(string,sizeof(char),   //600mb
						MAX_FILE_BUFFER, fp);      //LIMITS TO 100 MB
					fclose(fp);
					string[n] = '\0';
               ////////
               int var = 0;
               int start = 0;
               int j = 0;
               int hit = 0;
               ////////////////////////////////////////////////
               while(start < n){
               j = 0;
               // mOVE THIS PAST THE LOOP BEFORE JOB 2
//               usleep(TENMILLISEC);
               
               new_packet = (struct packet *)
                  malloc(sizeof(struct packet));


               new_packet->dst
                  = new_job->file_upload_dst;
               new_packet->src = (char) host_id;
               new_packet->type = PKT_FILE_UPLOAD_END;




               if(start+100 < n){
                  var = start+100;
               }
               else{
                  var = n;
               }
               
					for (i=start; i<var; i++) {
                  
						new_packet->payload[j] 
							= string[i];
                  j++;
					}


					new_packet->length = var-start;


               /*
					 * Create a job to send the packet
					 * and put the job in the job queue
					 */


             






					new_job2 = (struct host_job *)
						malloc(sizeof(struct host_job));
					new_job2->type 
						= JOB_SEND_PKT_ALL_PORTS; 
					new_job2->packet = new_packet;
					job_q_add(&job_q, new_job2);




               if(var >= n){
                 //printf("IN\n"); 
                  break;
                     
               }
               else{
                  //printf("OUT\n");
                  start += 100;
                  
               }




               //printf("TESTVAR2 %d\n",testvar);
               //free(new_job);
               }
               
  
				}


            else {  
					/* Didn't open file */
				}
			}
			break;


      case JOB_FILE_DOWNLOAD_SEND:


			/* Open file */


         if (dir_valid == 1) {


            //BECAUSE OF THE PING THE DIR IS NOW THE SOURCE DIR OF THE FILE
				n = sprintf(name, "./%s/%s", 
					dir, new_job->fname_download);
				name[n] = '\0';
            //printf("TRYING TO OPEN %s\n",name);
            fp = fopen(name, "r");
            
				if (fp != NULL) {
				   /* 
					 * Create first packet which
					 * has the file name 
					 */
               new_packet = (struct packet *) 


                  malloc(sizeof(struct packet));
					new_packet->dst = new_job->file_download_src;
					new_packet->src
                  = (char) host_id;
					new_packet->type 
						= PKT_FILE_DOWNLOAD_START;
					for (i=0; 
						new_job->fname_download[i]!= '\0'; 
						i++) {
						new_packet->payload[i] = 
							new_job->fname_download[i];
					}
					new_packet->length = i;
				//	printf("\n\nThe payload is %s\n\n",new_packet->payload);


					/* Create a job to send the packet
					 * and put it in the job queue
                */
					 
					new_job2 = (struct host_job *)
						malloc(sizeof(struct host_job));
					new_job2->type = JOB_SEND_PKT_ALL_PORTS;
					new_job2->packet = new_packet;
					job_q_add(&job_q, new_job2);
            //printf("\n\nCHECK NAME IS name is %s\n\n",name);


					/* 
					 * Create the second packet which
					 * has the file contents
					 */
               //if(fp != NULL){				
					n = fread(string,sizeof(char),   //600mb
						MAX_FILE_BUFFER, fp);      //LIMITS TO 100 MB
					fclose(fp);
					string[n] = '\0';
               //printf("THIS STRING IS %s\n",string);
               int var = 0;
               int start = 0;
               int j = 0;
               int hit = 0;
               
               while(start < n){
                  j = 0;
               
                  new_packet = (struct packet *)
                     malloc(sizeof(struct packet));
                  new_packet->dst
                     = new_job->file_download_src;
                  new_packet->src = (char) host_id;
                  new_packet->type = PKT_FILE_DOWNLOAD_END;
                  if(start+100 < n){
                     var = start+100;
                  }
                  else{
                     var = n;
                  }
               
					   for (i=start; i<var; i++) {
						   new_packet->payload[j] = string[i];
                     j++;
					   }
              //    printf("YO IM LOOKING AT THIS PAYLOAD %s\n",new_packet->payload);


					   new_packet->length = var-start;
               /*
					 * Create a job to send the packet
					 * and put the job in the job queue
					 */
					   new_job2 = (struct host_job *)
						   malloc(sizeof(struct host_job));
					   new_job2->type 
						   = JOB_SEND_PKT_ALL_PORTS; 
					   new_job2->packet = new_packet;
					   job_q_add(&job_q, new_job2);


                  if(var >= n){
                     break;   
                  }
                  else{
                     start += 100;
                  }


               }


          //     printf("YEAH I AM HERE\n");
  
				}




            else {  
					/* Didn't open file */
				}
			}
			break;


      case JOB_FILE_DOWNLOAD_RECV_START:


         /* Initialize the file buffer data structure */
         file_buf_init(&f_buf_upload);


         /*
          * Transfer the file name in the packet payload
          * to the file buffer data structure
          */




         //Maybe add a loop here?
         file_buf_put_name(&f_buf_upload,
            new_job->packet->payload,
            new_job->packet->length);


         free(new_job->packet);
         free(new_job);
         break;




      case JOB_FILE_UPLOAD_RECV_START:


			/* Initialize the file buffer data structure */
			file_buf_init(&f_buf_upload);


			/* 
			 * Transfer the file name in the packet payload
			 * to the file buffer data structure
			 */
         //Maybe add a loop here?
			file_buf_put_name(&f_buf_upload, 
				new_job->packet->payload, 
				new_job->packet->length);


			free(new_job->packet);
			free(new_job);
         break;


      case JOB_FILE_DOWNLOAD_RECV_END:




         file_buf_add(&f_buf_upload,
            new_job->packet->payload,
            new_job->packet->length);


         //free(new_job->packet);
         //free(new_job);


         if (dir_valid == 1 && n < 100) {


            //printf("Writing\n");
            /*
             * Get file name from the file buffer
             * Then open the file
             */


            free(new_job->packet);
            free(new_job);


            file_buf_get_name(&f_buf_upload, string);
            n = sprintf(name, "./%s/%s", dir, string);
            name[n] = '\0';
            //printf("This is the string %s\n",string);
            fp = fopen(name, "w");
            //printf("iTRYING TO OPE N TO ACCES %s\n",name);
            if (fp != NULL) {
             //printf("INSIDE PF\n");
               /*
                * Write contents in the file
                * buffer into file
                */
               //printf("HEY THIS IS THE PAYLOAD %s\n",string);


               //printf("Writing Contents\n");
               while (f_buf_upload.occ > 0) {
                  n = file_buf_remove(
                     &f_buf_upload,
                     string,
                     PKT_PAYLOAD_MAX);
                  string[n] = '\0';
                  n = fwrite(string,
                     sizeof(char),
                     n,
                     fp);
               }


               fclose(fp);
            }
         }


         break;




		case JOB_FILE_UPLOAD_RECV_END:


			/* 
			 * Download packet payload into file buffer 
			 * data structure 
			 */


         file_buf_add(&f_buf_upload, 
				new_job->packet->payload,
				new_job->packet->length);
         
			//free(new_job->packet);
			//free(new_job);


			if (dir_valid == 1 && n < 100) {


				/* 
				 * Get file name from the file buffer 
				 * Then open the file
				 */


            free(new_job->packet);
            free(new_job);


				file_buf_get_name(&f_buf_upload, string);
				n = sprintf(name, "./%s/%s", dir, string);
				name[n] = '\0';
//            printf("This is the dir %s\n",name);
				fp = fopen(name, "w");


				if (fp != NULL) {
					/* 
					 * Write contents in the file
					 * buffer into file
					 */


               //printf("Writing Contents\n");
					while (f_buf_upload.occ > 0) {
						n = file_buf_remove(
							&f_buf_upload, 
							string,
							PKT_PAYLOAD_MAX);
						string[n] = '\0';
						n = fwrite(string,
							sizeof(char),
							n, 
							fp);
					}


					fclose(fp);
				}	
			}


			break;
		}


	}




	/* The host goes to sleep for 10 ms */
	usleep(TENMILLISEC);


} /* End of while loop */


}








