/*********************************************
 * vim:sw=8:ts=8:si:et
 * To use the above modeline in vim you must have "set modeline" in your .vimrc
 * Author: Guido Socher
 * Copyright: GPL V2
 *
 * Tuxgraphics AVR webserver/ethernet board
 *
 * http://tuxgraphics.org/electronics/
 * Chip type           : Atmega88/168/328 with ENC28J60
 *
 *
 * MODYFIKACJE: Miros�aw Karda� --- ATmega32
 * MODYFIKACJE: Robert Mleczko --- Silniki krokowe
 *
 *********************************************/
#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdlib.h>
#include <string.h>
#include "ip_arp_udp_tcp.h"
#include "enc28j60.h"
#include "util/delay.h"
#include "net.h"
#include "step.h"
#include "websrv_help_functions.h"

/*STEPPER*/
volatile uint8_t ms2_flag;
uint8_t steps = 0;
uint8_t step_cmd = 0;
uint8_t step_x = 0;
uint8_t start_stepper = 0;
/*END OF STEPPER*/


// ustalamy adres MAC
static uint8_t mymac[6] = {0x00,0x55,0x58,0x10,0x00,0x29};
// ustalamy adres IP urz�dzenia
static uint8_t myip[4] = {192,168,0,110};

// server listen port for www
#define MYWWWPORT 80

#define BUFFER_SIZE 850
static uint8_t buf[BUFFER_SIZE+1];
static char gStrbuf[25];

//ANALIZA URLA
int8_t analyse_get_url(char *str)
{
        //uint8_t loop=15;
        // the first slash:
        if (*str == '/'){
                str++;
        }else{
                return(-1);
        }
        /*
        if (strncmp("favicon.ico",str,11)==0){
                return(2);
        }
        // the password:
        if(verify_password(str)==0){
                return(-1);
        }
        // move forward to the first space or '/'
        while(loop){
                if(*str==' '){
                        // end of url and no slash after password:
                        return(-2);
                }
                if(*str=='/'){
                        // end of password
                        loop=0;
                        continue;
                }
                str++;
                loop--; // do not loop too long
        }*/
        // str is now something like password?sw=1 or just end of url
        if (find_key_val(str,gStrbuf,5,"sw")){
                if (gStrbuf[0]=='0'){
                        return(0);
                }
                if (gStrbuf[0]=='1'){
                        return(1);
                }
                if(gStrbuf[0]=='2'){
                	step_cmd = 2;
                	//return(2);
                }
                if(gStrbuf[0]=='3'){
                	return(3);
                }
        }

        if (step_cmd==2){
                	if(find_key_val(str, gStrbuf,5,"ox")){
                		step_x = atoi(gStrbuf);
                		return(2);
                	}
        }
        return(-3);
}
//KONIEC ANALIZY URLA

uint16_t http200ok(void)
{
        return(fill_tcp_data_p(buf,0,PSTR("HTTP/1.0 200 OK\r\nContent-Type: text/html\r\nPragma: no-cache\r\n\r\n")));
}

// prepare the webpage by writing the data to the tcp send buffer
uint16_t print_webpage(uint8_t *buf, uint8_t on)
{
        uint16_t plen;
        plen=http200ok();
        plen=fill_tcp_data_p(buf,plen,PSTR("<pre>"));
        plen=fill_tcp_data_p(buf,plen,PSTR("<font color='green' size='6'><b>Witaj !</b>\n</font>"));
        plen=fill_tcp_data_p(buf,plen,PSTR("<font color='blue'><i>tw�j serwer www dzia�a znakomicie</i>\n\n</font>"));
        if(on){
               	   plen=fill_tcp_data_p(buf,plen,PSTR(" <font color=#00FF00>ON</font>"));
                   plen=fill_tcp_data_p(buf,plen,PSTR(" <a href=\"./?sw=0\">[switch off]</a>\n"));
                   plen=fill_tcp_data_p(buf,plen,PSTR(" <a href=\"./?sw=0\">Moj_off</a>\n"));
              }
        else{
                   plen=fill_tcp_data_p(buf,plen,PSTR("OFF"));
                   plen=fill_tcp_data_p(buf,plen,PSTR(" <a href=\"./?sw=1\">[switch on]</a>\n"));
                   plen=fill_tcp_data_p(buf,plen,PSTR(" <a href=\"./?sw=1\">Moj_on</a>\n"));
            }

        /*STEPPER*/
                   plen=fill_tcp_data_p(buf,plen,PSTR("<hr><br><form METHOD=get action=\""));
                   plen=fill_tcp_data_p(buf,plen,PSTR("\">\n<input type=hidden name=sw value=2>\n<input size=20 type=text name=ox>\n<br><input size=20 type=text name=oy>\n"));
                   plen=fill_tcp_data_p(buf,plen,PSTR("\">\n<br><input type=submit value=\"MOVE STEPPER\"></form>\n"));
                   plen=fill_tcp_data_p(buf,plen,PSTR(" <a href=\"./?sw=3\">TURN OFF STEPPER</a>\n"));


        plen=fill_tcp_data_p(buf,plen,PSTR("\n<a href=\".\">[refresh status]</a>\n"));
        plen=fill_tcp_data_p(buf,plen,PSTR("</pre>\n"));
        return(plen);
}



int main(void){
/* ustawienie TIMER0 dla F_CPU=16MHz */
		TCCR0 |= (1<<WGM01);				/* tryb CTC */
		TCCR0 |= (1<<CS02)|(1<<CS00);		/* preskaler = 1024 */
		OCR0 = 39;							//przepelnienie dla 400Hz IDEANE KROKI!
		TIMSK |= (1<<OCIE0);				/* zezwolenie na przerwanie CompareMatch */
/* przerwanie wykonywane z cz�stotliwo�ci� ok 2,5ms (400 razy na sekund�) */

		sei();
        uint16_t dat_p;
        uint8_t cmd;
        uint16_t plen;

        silnik_stop();

        // Dioda LED na PD7:
        DDRD|= (1<<DDD7);
        PORTD &= ~(1<<PORTD7);// Dioda OFF

        //initialize the hardware driver for the enc28j60
        enc28j60Init(mymac);
        enc28j60PhyWrite(PHLCON,0x476);
        
        //init the ethernet/ip layer:
        init_ip_arp_udp_tcp(mymac,myip,MYWWWPORT);

        sei();

        while(1){
        	//if(ms2_flag){				//krecenie motorkiem bez przerwy
        		//kroki_lewo();
        		//ms2_flag=0;
        	//}
                // read packet, handle ping and wait for a tcp packet:
                dat_p=packetloop_icmp_tcp(buf,enc28j60PacketReceive(BUFFER_SIZE, buf));


                if(start_stepper)
               	{
                	if(ms2_flag){				//krecenie motorkiem bez przerwy
                		kroki_lewo();
                        ms2_flag=0;
                    }
                }



//!!!!!!!!!!	REQUESTY DO SILNIKA PRZED TYM KOMENTARZEM BO if(dat_p==0)
//PRZY DRUGIM PRZEBIEGU PETLI OMIJA CALEGO while() !!!!!!!!!!

                /* dat_p will be unequal to zero if there is a valid 
                 * http get */
                if(dat_p==0){
                        // no http request
                        continue;
                }
                // tcp port 80 begin
                if (strncmp("GET ",(char *)&(buf[dat_p]),4)!=0){
                        // head, post and other methods:
                        plen=http200ok();
                        plen=fill_tcp_data_p(buf,plen,PSTR("<h1>200 OK</h1>"));
                        goto SENDTCP;
                }
                // just one web page in the "root directory" of the web server
                if (strncmp("/ ",(char *)&(buf[dat_p+4]),2)==0){
                		plen=http200ok();
						plen=print_webpage(buf,(PORTD & (1<<PORTD7)));
                        goto SENDTCP;
                }//else{
                   //     dat_p=fill_tcp_data_p(buf,0,PSTR("HTTP/1.0 401 Unauthorized\r\nContent-Type: text/html\r\n\r\n<h1>401 Unauthorized</h1>"));
                     //   goto SENDTCP;
                //}
                cmd=analyse_get_url((char *)&(buf[dat_p+4]));
                                // for possible status codes see:
                                // http://www.w3.org/Protocols/rfc2616/rfc2616-sec10.html
                                if (cmd==-1){
                                        plen=fill_tcp_data_p(buf,0,PSTR("HTTP/1.0 401 Unauthorized\r\nContent-Type: text/html\r\n\r\n<h1>401 Unauthorized</h1>"));
                                        goto SENDTCP;
                                }
                                if (cmd==1){
                                        //LEDON;
                                        PORTD|= (1<<PORTD7);// transistor on
                                }
                                if (cmd==0){
                                        //LEDOFF;
                                        PORTD &= ~(1<<PORTD7);// transistor off
                                }
                                if (cmd==2){
                                       start_stepper = 1;
                                }
                                if (cmd==3)
                                {
                                	start_stepper = 0;
                                	silnik_stop();
                                }
                                /*
                                if (cmd==-2){
                                        // redirect to the right base url (e.g add a trailing slash):
                                        plen=moved_perm(buf,1);
                                        goto SENDTCP;
                                }*/
                                // if (cmd==-2) or any other value
                                // just display the status:
                                plen=print_webpage(buf,(PORTD & (1<<PORTD7)));
SENDTCP:
				www_server_reply(buf,plen);
                // tcp port 80 end

        }
        return (0);
}




/* ================= PROCEDURA OBS�UGI PRZERWANIA � COMPARE MATCH */
/* pe�ni funkcj� timera programowego wyznaczaj�cego podstaw� czasu = 2,5ms */
ISR(TIMER0_COMP_vect){
		ms2_flag = 1;	/* ustawiamy flag� co 2,5ms */
	}

