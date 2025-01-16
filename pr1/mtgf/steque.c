#include <stdlib.h>
#include <stdio.h>
#include "steque.h"

void steque_init(steque_t *queue){
  queue->front = NULL;
  queue->back = NULL;
  queue->N = 0;
}

void steque_enqueue(steque_t* queue, steque_item item){
  steque_node_t* node;

  node = (steque_node_t*) malloc(sizeof(steque_node_t));
  node->item = item;
  node->next = NULL;
  
  if(queue->back == NULL)
    queue->front = node;
  else
    queue->back->next = node;

  queue->back = node;
  queue->N++;
}

void steque_push(steque_t* queue, steque_item item){
  steque_node_t* node;

  node = (steque_node_t*) malloc(sizeof(steque_node_t));
  node->item = item;
  node->next = queue->front;

  if(queue->back == NULL)
    queue->back = node;
  
  queue->front = node;
  queue->N++;
}

int steque_size(steque_t* queue){
  return queue->N;
}

int steque_isempty(steque_t *queue){
  return queue->N == 0;
}

steque_item steque_pop(steque_t* queue){
  steque_item ans;
  steque_node_t* node;
  
  if(queue->front == NULL){
    fprintf(stderr, "Error: underflow in steque_pop.\n");
    fflush(stderr);
    exit(EXIT_FAILURE);
  }

  node = queue->front;
  ans = node->item;

  queue->front = queue->front->next;
  if (queue->front == NULL) queue->back = NULL;
  free(node);

  queue->N--;

  return ans;
}

void steque_cycle(steque_t* queue){
  if(queue->back == NULL)
    return;
  
  queue->back->next = queue->front;
  queue->back = queue->front;
  queue->front = queue->front->next;
  queue->back->next = NULL;
}

steque_item steque_front(steque_t* queue){
  if(queue->front == NULL){
    fprintf(stderr, "Error: underflow in steque_front.\n");
    fflush(stderr);
    exit(EXIT_FAILURE);
  }
  
  return queue->front->item;
}

void steque_destroy(steque_t* queue){
  while(!steque_isempty(queue))
    steque_pop(queue);
}
