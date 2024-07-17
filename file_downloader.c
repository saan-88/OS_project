#include <stdio.h> 
#include <stdlib.h> 
#include <pthread.h> 
#include <curl/curl.h> 
#include <string.h> 
#include <sys/time.h> 
#include <unistd.h> 
#define CHUNK_SIZE 1024 * 1024 // Define the size of a chunk (1MB)

// Struct to hold data for each thread
typedef struct {
    char *url;
    long start;
    long end;
    FILE *output_fp;
    pthread_mutex_t *mutex;
} ThreadData;

// Callback function to write data to a file
size_t write_callback(void *ptr, size_t size, size_t nmemb, void *stream) {
    ThreadData *data = (ThreadData *)stream;
    pthread_mutex_lock(data->mutex);
    fseek(data->output_fp, data->start, SEEK_SET);
    size_t written = fwrite(ptr, size, nmemb, data->output_fp);
    data->start += written;
    pthread_mutex_unlock(data->mutex);
    return written;
}

void *download_chunk(void *arg) {
    ThreadData *data = (ThreadData *)arg;
    CURL *curl;
    CURLcode res;
    char range[64]; 

    snprintf(range, sizeof(range), "%ld-%ld", data->start, data->end);

    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, data->url); 
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, data);
        curl_easy_setopt(curl, CURLOPT_RANGE, range); 

        res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        }

        curl_easy_cleanup(curl);
    }

    return NULL;
}

// Function to extract filename from URL
const char *extract_filename_from_url(const char *url) {
    const char *filename = url;

    for (const char *p = url; *p; ++p) {
        if (*p == '/') {
            filename = p + 1;
        }
    }

    if (*filename) {
        return filename;
    }

    return "output.file";
}

// Main function
int main(int argc, char *argv[]) {
    char *url = NULL;
    char *output_file = NULL;
    int num_threads = 4; // Default number of threads
    int verbose = 0;

    int opt;
    while ((opt = getopt(argc, argv, "t:o:v")) != -1) {
        switch (opt) {
            case 't':
                num_threads = atoi(optarg);
                break;
            case 'o':
                output_file = optarg;
                break;
            case 'v':
                verbose = 1;
                break;
            default:
                fprintf(stderr, "Usage: %s [-t num_threads] [-o output_file] [-v] <URL>\n", argv[0]);
                return 1;
        }
    }

    if (optind < argc) {
        url = argv[optind];
    } else {
        fprintf(stderr, "Usage: %s [-t num_threads] [-o output_file] [-v] <URL>\n", argv[0]);
        return 1;
    }

    // If no output file specified, extract filename from URL
    if (output_file==NULL) {
        output_file = (char *)extract_filename_from_url(url);
    }

    // printf("URL: %s\n", url); // Print the URL
    // printf("num_threads: %d, output file: %s\n", num_threads, output_file);

    // Step 1: Getting filesize
    CURL *curl;
    CURLcode res;
    double filesize = 0.0;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();

    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
        if (verbose) {
            curl_easy_setopt(curl, CURLOPT_HEADER, 1L);
        }

        res = curl_easy_perform(curl);
        if (res == CURLE_OK) {
            curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &filesize);
        } else {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
            return 1;
        }

        curl_easy_cleanup(curl);
    }

    // End of getting filesize

    // Open the output file for writing
    FILE *output_fp = fopen(output_file, "wb");
    if (output_fp == NULL) {
        perror("fopen"); 
        return 1;
    }

    // Declare arrays for threads and their data
    pthread_t threads[num_threads];
    ThreadData thread_data[num_threads];
    pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;

    long chunk_size = (long)filesize / num_threads;

//Initialize threads to download each chunk
    for (int i = 0; i < num_threads; ++i) {
        thread_data[i].url = url; 
        thread_data[i].start = i * chunk_size;

        if(i == num_threads - 1){
            thread_data[i].end = (long)filesize - 1;
        }
        else{
            thread_data[i].end=(i + 1) * chunk_size - 1;
        }
        
        thread_data[i].output_fp = output_fp;
        thread_data[i].mutex = &file_mutex;

        // Create the thread and start downloading
        pthread_create(&threads[i], NULL, download_chunk, &thread_data[i]);
    }

    // Wait for all threads to finish
    for (int i = 0; i < num_threads; ++i) {
        pthread_join(threads[i], NULL);
    }

    fclose(output_fp); // Close the output file

    curl_global_cleanup(); // Clean up cURL globally

    return 0;
}
