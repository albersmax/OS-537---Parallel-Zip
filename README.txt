COMPSCI 537
11/9/2021

Max Albers, Tim Bostanche

Parallel Processing of wzip Compression

In order to organize the writing of compressed files to stdout we implemented a producer/consumer
relationship. The producer fills out the buffer structure with information from each page of each 
input file, making sure to allocate extra space for files that aren't page-aligned, and fills a
circular queue. The queue uses a FIFO policy; consumer threads take a buffer object and compresses
it according to the wzip specification, the resulting information is then passed to an output
object with a function that uses the buffer information to find its correct index in the output
string. Once the threads are finished processing the pages, the first and last character of each 
page is compared to ensure cross-page and cross-file character sequences are combined. We felt that 
using mmap to organize the page data and using consumers to process pages was a good tradeoff 
for situations where there were varying file sizes. With our program, threads aren't working on
files or lines of files, instead they work on a page of memory at a time and progress through all
files in one swoop.

