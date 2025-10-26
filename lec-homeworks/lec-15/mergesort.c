// Implementation of merge sort in C using pthreads
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

struct mergesortArgs
{
    int *array;
    int n;
};

// Function to perform merge sort on an array provided as input
void *mergeSort(void *args)
{
    struct mergesortArgs *msArgs = (struct mergesortArgs *)args;
    int *array = msArgs->array;
    int n = msArgs->n;
    // Check for base case
    if (n < 2)
        return;
    int mid = n / 2;
    // mergeSort(array, mid);
    // mergeSort(array + mid, n - mid);

    pthread_t left_thread, right_thread;
    struct mergesortArgs leftArgs = {array, mid};
    struct mergesortArgs rightArgs = {array + mid, n - mid};

    printf("Creating threads for left half of size %d and right half of size %d\n", mid, n - mid);
    pthread_create(&left_thread, NULL, mergeSort, (void *)&leftArgs);
    pthread_create(&right_thread, NULL, mergeSort, (void *)&rightArgs);

    printf("Waiting for threads to finish\n");
    pthread_join(left_thread, NULL);
    pthread_join(right_thread, NULL);

    // Merge the sorted halves
    int *temp = malloc(n * sizeof(int));
    if (!temp)
    {
        perror("malloc");
        return;
    }
    int i = 0, j = mid, k = 0;
    while (i < mid && j < n)
    {
        if (array[i] < array[j])
            temp[k++] = array[i++];
        else
            temp[k++] = array[j++];
    }
    // Copy any remaining elements from either half
    while (i < mid)
        temp[k++] = array[i++];
    while (j < n)
        temp[k++] = array[j++];
    // Copy the merged elements back into the original array
    for (i = 0; i < n; i++)
        array[i] = temp[i];
    free(temp);
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <number_of_elements>\n", argv[0]);
        return EXIT_FAILURE;
    }
    int n = atoi(argv[1]);
    if (n <= 0)
    {
        fprintf(stderr, "Number of elements must be positive.\n");
        return EXIT_FAILURE;
    }

    int *array = malloc(n * sizeof(int));
    if (!array)
    {
        perror("malloc");
        return EXIT_FAILURE;
    }

    // Initialize array with random integers
    for (int i = 0; i < n; i++)
    {
        array[i] = rand() % 1000; // Random integers between 0 and 999
    }

    // Print unsorted array
    printf("Unsorted array:\n");
    for (int i = 0; i < n; i++)
    {
        printf("%d ", array[i]);
    }
    printf("\n");

    // Perform merge sort
    struct mergesortArgs args = {array, n};
    mergeSort((void *)&args);

    // Print sorted array
    printf("Sorted array:\n");
    for (int i = 0; i < n; i++)
    {
        printf("%d ", array[i]);
    }
    printf("\n");

    free(array);
    return EXIT_SUCCESS;
}