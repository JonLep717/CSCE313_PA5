#include <fstream>
#include <iostream>
#include <stdio.h>  // for fseek()
#include <thread>
#include <vector>
#include <sys/time.h>
#include <sys/wait.h>
#include <utility>      // for std::pair

#include "BoundedBuffer.h"
#include "common.h"
#include "Histogram.h"
#include "HistogramCollection.h"
#include "TCPRequestChannel.h"

// for client:
//  - a option: IP address (default 127.0.0.1) local host
//  - r option: port number (default 8080)

// for server:
//  - r option: port number (default 8080)

// for TCPRequestChannel:
//  - in client/server, replace request channel instances with TCPRequestChannel

// Change all instances of FIFOReqChan to TCPRequestChannel
// change to the appropriate constructor

// To the getopt loop, add a (IPaddress) and r options (port#)

// ecgno to use for datamsgs
#define ECGNO 1

using namespace std;

__int64_t get_file_size(TCPRequestChannel *c, string file_name) {
	__int64_t offset = 0;
	__int64_t length = 0;
	int packet_size = sizeof(filemsg) + file_name.size() + 1;
	filemsg file_message = filemsg(offset, length);
	char *packet = new char[packet_size];

	memcpy(packet, &file_message,sizeof(filemsg));
	strcpy(packet + sizeof(filemsg), file_name.c_str());
	c->cwrite(packet,packet_size);
	__int64_t filesize;
	c->cread(&filesize, sizeof(__int64_t));	
	delete[] packet;

	return filesize;
}
// GOTTA CHANGE THIS
/*
TCPRequestChannel *new_chan(TCPRequestChannel *c) {
	MESSAGE_TYPE new_mess = NEWCHANNEL_MSG;
    c->cwrite(&new_mess, sizeof(MESSAGE_TYPE));
	char new_chan_name[30];
	c->cread(&new_chan_name, sizeof(string));
	TCPRequestChannel* chan2 = new TCPRequestChannel(new_chan_name, TCPRequestChannel::CLIENT_SIDE);
	return chan2;
}
*/
void clean_string_format(string &in) {		// function to remove trailing zeros from integer string
	in.erase(in.find_last_not_of('0') + 1, string::npos);
	in.erase(in.find_last_not_of('.') + 1, string::npos);
}

void patient_thread_function (int num_req, BoundedBuffer* req_buf, int patient) {
    // functionality of the patient threads
    double time = 0;
    for (double i=0; i < num_req; i++){
		datamsg x1(patient, time, ECGNO);

        req_buf->push((char*) &x1, sizeof(datamsg));
        time += 0.004;
    }
}

void file_thread_function (string filename, __int64_t filesize, BoundedBuffer* req_buf, int buffercapacity) {
    // functionality of the file thread

    __int64_t offset = 0;
	double num_trans = ceil(filesize/buffercapacity);
    int packet_size = sizeof(filemsg) + filename.size() + 1;
    char *buffer = new char[packet_size];
	int transfers = 0;
	__int64_t buffercap = buffercapacity;
	while (transfers <= num_trans) {
		if (transfers == num_trans) {
			buffercap = filesize - offset;
            if (buffercap == 0) {
                break;
            }
		}
		filemsg trans_filemsg = filemsg(offset, buffercap);
        memcpy(buffer, &trans_filemsg, sizeof(filemsg));
        strcpy(buffer + sizeof(filemsg), filename.c_str());
        req_buf->push(buffer, packet_size);

        offset += buffercap;
        transfers++;
	}
    delete[] buffer;
}

void worker_thread_function (BoundedBuffer* req_buf, BoundedBuffer* response_buf, TCPRequestChannel* channel, string f, int m) {
    char *buf = new char[m];
    
    while(true) {
        req_buf->pop(buf, m);
        MESSAGE_TYPE* m_type = (MESSAGE_TYPE*) buf;
        if (*m_type == DATA_MSG) {
            datamsg *dat_msg = (datamsg*) buf;
            int patient = dat_msg->person;
            channel->cwrite(buf, sizeof(datamsg));
            double reply;
            channel->cread(&reply, sizeof(double));

            pair <int,double> response_pair(patient,reply);
            response_buf->push((char*)&response_pair, sizeof(pair<int,double>));
        }

        else if (*m_type == FILE_MSG) {
            filemsg *file_msg = (filemsg*) buf;
            __int64_t offset = file_msg->offset;
            int buffer_size = file_msg->length;
            int packet_size = sizeof(filemsg) + f.size() + 1;
            char *received_packet = new char[buffer_size];

            channel->cwrite(buf, packet_size);
            channel->cread(received_packet, buffer_size);

            string file_path = "received/" + f;
            FILE* output_file;
            output_file = fopen(file_path.c_str(), "rb+");
            fseek(output_file, offset, SEEK_SET);
            fwrite(received_packet, buffer_size, 1, output_file);
            fclose(output_file); 
            delete[] received_packet;   
        }

        else if (*m_type == QUIT_MSG) {
            channel->cwrite(m_type, sizeof(MESSAGE_TYPE));
            delete channel;
            break;
        }
    }
    delete[] buf;
    // functionality of the worker threads
}

void histogram_thread_function (BoundedBuffer* response_buf, HistogramCollection* histograms) {
    // functionality of the histogram threads
    while(true) {
        pair<int,double> response_pair;
        char buf[sizeof(pair<int,double>)];
        response_buf->pop(buf, sizeof(pair<int,double>));
        response_pair = *(pair<int,double>*) buf;
        int patient_no = response_pair.first;
        double ecg = response_pair.second;

        if (patient_no < 0) {
            break;
        }
        else {
            histograms->update(patient_no,ecg);
        }
    }
}


int main (int argc, char* argv[]) {
    string a = "127.0.0.1"; // default IP address
    string r = "8080";      // default port number
    int n = 1000;	// default number of requests per "patient"
    int p = 10;		// number of patients [1,15]
    int w = 100;	// default number of worker threads
	int h = 20;		// default number of histogram threads
    int b = 20;		// default capacity of the request buffer (should be changed)
	int m = MAX_MESSAGE;	// default capacity of the message buffer
	string f = "";	// name of file to be transferred
    // read arguments
    int opt;
	while ((opt = getopt(argc, argv, "n:p:w:h:b:m:f:a:r:")) != -1) {
		switch (opt) {
			case 'n':
				n = atoi(optarg);
                break;
			case 'p':
				p = atoi(optarg);
                break;
			case 'w':
				w = atoi(optarg);
                break;
			case 'h':
				h = atoi(optarg);
				break;
			case 'b':
				b = atoi(optarg);
                break;
			case 'm':
				m = atoi(optarg);
                break;
			case 'f':
				f = optarg;
                break;
            case 'a':
                a = optarg;
                break;
            case 'r':
                r = optarg;
                break;
		}
	}
    
	// fork and exec the server
    /*
    int pid = fork();
    if (pid == 0) {
        execl("./server", "./server", "-m", (char*) to_string(m).c_str(), nullptr);
    }
    */
    
	// initialize overhead (including the control channel)
    //TCPRequestChannel* chan = new TCPRequestChannel("control", TCPRequestChannel::CLIENT_SIDE);     // GOTTA CHANGE THIS
    BoundedBuffer *request_buffer = new BoundedBuffer(b);
    BoundedBuffer *response_buffer = new BoundedBuffer(b);
	HistogramCollection* hc = new HistogramCollection();
    // array/vector of producer threads (if data transfer, p elements; if file, 1 element)
    vector<thread> producer_threads;
    // array of FIFOs (w elements)
    vector<TCPRequestChannel*> fifo_vector;
    // array of worker threads (w elements)
    vector<thread> worker_threads;
    // array of histogram threads (if data: h elements; if file: 0 elements)
    vector<thread> hist_threads;
    // making histograms and adding to collection

    for (int i = 0; i < p; i++) {
        Histogram* h = new Histogram(10, -2.0, 2.0);
        hc->add(h);
    }
	
	// record start time
    struct timeval start, end;
    gettimeofday(&start, 0);

    /* create all threads here */
    // if data:
    //      - create p patient threads (store in producer threads array)
    
    if (f == "") {
        for (int i = 1; i <= p; i++) {
            // thread p_thread(patient_thread_function, n, request_buffer, i);
            producer_threads.push_back(thread(patient_thread_function, n, request_buffer, i));
        }
    //      - create w worker threads (store in worker array)
        for (int i = 0; i < w; i++) {
            TCPRequestChannel* worker_channel = new TCPRequestChannel(a,r);
            fifo_vector.push_back(worker_channel);
            worker_threads.push_back(thread(worker_thread_function, request_buffer, response_buffer, worker_channel, f, m));
        }
        for (int i = 0; i < h; i++) {
            hist_threads.push_back(thread(histogram_thread_function,response_buffer, hc));
        }
    }

    else if (f != "") {
        for (int i = 0; i < w; i++) {
            TCPRequestChannel* worker_channel = new TCPRequestChannel(a,r);
            fifo_vector.push_back(worker_channel);
            worker_threads.push_back(thread(worker_thread_function, request_buffer, response_buffer, worker_channel, f, m));
        }
        __int64_t file_size = get_file_size(fifo_vector[0], f);
        string file_path = "received/" + f;
        FILE* output_file = fopen(file_path.c_str(), "wb+");
        fclose(output_file);
        //producer_threads.push_back(thread(file_thread_function, f, file_size,request_buffer, m));
        producer_threads.push_back(thread(file_thread_function, f, file_size,request_buffer, m));
    }
    //      - create w worker threads (store in worker array)

    //          -> create channel for worker thread (store in FIFO array)
    //      - create h histogram threads (store in histogram array)
    

	/* join all threads here */
    // joining p threads
    if (f == "") {
        for (int i = 0; i < p; i++) {
            producer_threads[i].join();
        }
    }
    else {
        producer_threads[0].join();
    }
    
    // send w quit messages to request buffer
    for (int i = 0; i < w; i++) {
        MESSAGE_TYPE q_msg = QUIT_MSG;
        request_buffer->push((char*) &q_msg, sizeof(MESSAGE_TYPE));
    }

    // joining w threads
    for (int i = 0; i < w; i++) {
        worker_threads[i].join();
    }

    if (f == "") {
        for (int i = 0; i < h; i++) {
            pair<int,double> close_hist(-1,0.0);    // close histogram message
            response_buffer->push((char*) &close_hist, sizeof(pair<int,double>));
        }

        // joining h threads
        for (int i = 0; i < h; i++) {
            hist_threads[i].join();
        }
    }    
	// record end time
    gettimeofday(&end, 0);

    // print the results
	if (f == "") {
		hc->print();
	}
    int secs = ((1e6*end.tv_sec - 1e6*start.tv_sec) + (end.tv_usec - start.tv_usec)) / ((int) 1e6);
    int usecs = (int) ((1e6*end.tv_sec - 1e6*start.tv_sec) + (end.tv_usec - start.tv_usec)) % ((int) 1e6);
    std::cout << "Took " << secs << " seconds and " << usecs << " micro seconds" << endl;

	// quit and close control channel
    //MESSAGE_TYPE q = QUIT_MSG;
    //chan->cwrite ((char *) &q, sizeof (MESSAGE_TYPE));
    std::cout << "All Done!" << endl;
    //delete chan;

    delete hc;
    delete request_buffer;
    delete response_buffer;

    /*
	// wait for server to exit
	wait(nullptr);
    */
}
