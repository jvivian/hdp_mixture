//
//  distr_script.c
//  
//
//  Created by Jordan Eizenga on 1/17/16.
//
//

#include <stdio.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>
#include "nanopore_hdp.h"
#include "hdp_math_utils.h"

#define DISTR_DIR "./nanopore_distrs"
#define NUCLEOTIDES "ACGT"
#define FILENAME_BUFFER_LEN 100

char* kmer_from_index(int64_t index, const char* alphabet, int64_t alphabet_size, int64_t kmer_length) {
    char* kmer = (char*) malloc(sizeof(char) * (kmer_length + 1));
    int64_t index_remainder = index;
    kmer[kmer_length] = '\0';
    for (int64_t i = kmer_length - 1; i >= 0; i--) {
        kmer[i] = alphabet[index_remainder % alphabet_size];
        index_remainder /= alphabet_size;
    }
    return kmer;
}

void write_kmer_distr(NanoporeHDP* nhdp, char* kmer, double* eval_grid, int64_t grid_length) {
    char filename[FILENAME_BUFFER_LEN];
    sprintf(filename, "%s/%s_distr.txt", DISTR_DIR, kmer);
    FILE* out = fopen(filename, "w");
    
    double density;
    for (int64_t i = 0; i < grid_length - 1; i++) {
        density = get_nanopore_kmer_density(nhdp, eval_grid[i], kmer);
        fprintf(out, "%.17lg\n", density);
    }
    density = get_nanopore_kmer_density(nhdp, eval_grid[grid_length - 1], kmer);
    fprintf(out, "%.17lg", density);
    
    fclose(out);
}

void write_all_kmer_distrs(NanoporeHDP* nhdp, double* eval_grid, int64_t grid_length) {
    char x_filename[FILENAME_BUFFER_LEN];
    sprintf(x_filename, "%s/x_vals.txt", DISTR_DIR);
    FILE* x_vals = fopen(x_filename, "w");
    for (int64_t i = 0; i < grid_length - 1; i++) {
        fprintf(x_vals, "%.17lg\n", eval_grid[i]);
    }
    fprintf(x_vals, "%.17lg", eval_grid[grid_length - 1]);
    fclose(x_vals);
    
    int64_t alphabet_size = get_nanopore_hdp_alphabet_size(nhdp);
    int64_t kmer_length = get_nanopore_hdp_kmer_length(nhdp);
    char* alphabet = get_nanopore_hdp_alphabet(nhdp);
    
    int64_t num_kmers = power(alphabet_size, kmer_length);
    
    for (int64_t kmer_index = 0; kmer_index < num_kmers; kmer_index++) {
        char* kmer = kmer_from_index(kmer_index, alphabet, alphabet_size, kmer_length);
        
        write_kmer_distr(nhdp, kmer, eval_grid, grid_length);
        
        free(kmer);
    }
    
    free(alphabet);
}


int main(int argc, char** argv) {
    if (argc != 6 && argc != 7) {
        fprintf(stderr, "Usage: distr_script [alignment_file] [model_file] [burn_in] [num_samples] [thinning] (align_filter).\n");
        exit(EXIT_FAILURE);
    }
    
    char* alignment_filepath = argv[1];
    FILE* file_test = fopen(alignment_filepath, "r");
    if (file_test == NULL) {
        fprintf(stderr, "File %s does not exist.\n", alignment_filepath);
        exit(EXIT_FAILURE);
    }
    fclose(file_test);
    
    char* model_filepath = argv[2];
    file_test = fopen(model_filepath, "r");
    if (file_test == NULL) {
        fprintf(stderr, "File %s does not exist.\n", model_filepath);
        exit(EXIT_FAILURE);
    }
    fclose(file_test);
    
    int err = mkdir(DISTR_DIR, S_IRWXU);
    if (err == -1) {
        fprintf(stderr, "Distribution directory already exists. Remove or rename %s before continuing.\n",
                DISTR_DIR);
        exit(EXIT_FAILURE);
    }
    
    fprintf(stderr, "Initializing HDP...\n");
    
    char* filter = NULL;
    if (argc == 7) {
        filter = argv[6];
    }
    
    int64_t alphabet_size = 4;
    int64_t kmer_length = 6;
    
    double grid_start = 20.0;
    double grid_stop = 100.0;
    int64_t grid_length = 150;
    
    double base_gamma = 20.0;
    double leaf_gamma = 40.0;
    
    NanoporeHDP* nhdp = flat_hdp_model(NUCLEOTIDES, alphabet_size, kmer_length, base_gamma, leaf_gamma,
                                       grid_start, grid_stop, grid_length, model_filepath);
    
    fprintf(stderr, "Initializing data...\n");
    
    if (argc == 7) {
        update_nhdp_from_alignment(nhdp, alignment_filepath, false);
    }
    else {
        update_nhdp_from_alignment_with_filter(nhdp, alignment_filepath, false, filter);
    }
    
    fprintf(stderr, "Executing Gibbs sampling...\n");
    
    int64_t burn_in = atoi(argv[3]);
    int64_t num_samples = atoi(argv[4]);
    int64_t thinning = atoi(argv[5]);
    
    execute_nhdp_gibbs_sampling(nhdp, num_samples, burn_in, thinning, true);
    
    finalize_nhdp_distributions(nhdp);
    
    fprintf(stderr, "Writing distributions to disk...\n");
    
    double* eval_grid = linspace(grid_start, grid_stop, grid_length);
    
    write_all_kmer_distrs(nhdp, eval_grid, grid_length);
    
    free(eval_grid);
    
    destroy_nanopore_hdp(nhdp);    
}
