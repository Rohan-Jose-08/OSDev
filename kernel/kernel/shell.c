#include <stdbool.h>

void shellinit() // edit as appropriate for your kernel
{
    while (true) // you may want to provide a built-in "exit" command
    {
        char* command;
        int proc;

        output_prompt();               // display a prompt
        command = input_line();        // get a line of input, which will become the command to execute
        proc = process_start(command); // start a process from the command
        free(command);

        while (process_executing(proc))
        {
            if (input_line_waiting())
            {
                char* line;
                line = input_line();                 // read input from user
                process_send_input_line(proc, line); // send input to process
                free(line);
            }
            if (process_output_line_waiting(proc))
            {
                char* output;
                output = process_get_output_line(proc); // get output from process
                output_line(output);                    // write output to user
                free(output);
            }
        }
    }
}

static void output_prompt()
{
    output_line(">");
}

static void output_line(char* line)
{
    printf("%s\n", line);
}

static int input_line_waiting()
{
    // TODO: perhaps someone more familiar with the C library could write a better example?

}

static char* input_line()
{
    // TODO: perhaps someone more familiar with the C library could write a better example?
    // TODO: this function will allocate a char buffer containing the line read
    
}

static int process_start(char* command)
{
    exec(command);
    // TODO: needs to return an int identifying this process
}

static int process_executing(int proc)
{
    // TODO: what would be a good example here?
}

static void process_send_input_line(int proc, char* line)
{
    // TODO: what would be a good example here?
}

static int process_output_line_waiting(int proc)
{
    // TODO: what would be a good example here?
}

static char* process_get_output_line(int proc)
{
    // TODO: what would be a good example here?
    // TODO: this function will allocate a char buffer containing the line read
}