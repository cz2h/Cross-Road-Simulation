#include <stdio.h> 
#include <stdlib.h> 
#include <string.h> 
#include <unistd.h> 
#include "traffic.h"

extern struct intersection isection;

/**
 * Populate the car lists by parsing a file where each line has
 * the following structure:
 *
 * <id> <in_direction> <out_direction>
 *
 * Each car is added to the list that corresponds with 
 * its in_direction
 * 
 * Note: this also updates 'inc' on each of the lanes
 */
void parse_schedule(char * file_name) {
    int id;
    struct car * cur_car;
    struct lane * cur_lane;
    enum direction in_dir, out_dir;
    FILE * f = fopen(file_name, "r");

    /* parse file */
    while (fscanf(f, "%d %d %d", & id, (int * ) & in_dir, (int * ) & out_dir) == 3) {

        /* construct car */
        cur_car = malloc(sizeof(struct car));
        cur_car-> id = id;
        cur_car-> in_dir = in_dir;
        cur_car-> out_dir = out_dir;

        /* append new car to head of corresponding list */
        cur_lane = & isection.lanes[in_dir];
        cur_car-> next = cur_lane-> in_cars;
        cur_lane-> in_cars = cur_car;
        cur_lane-> inc++;
    }

    fclose(f);
}

/**
 * TODO: Fill in this function
 *
 * Do all of the work required to prepare the intersection
 * before any cars start coming
 * 
 */
void init_intersection() {
    /* Want to initialize isection */

    /* Initialize the intersection.*/
    for (int i = 0; i < 4; i++) {
        
        pthread_mutex_init(&isection.quad[i], NULL);

    }

    /* Initialize variables in each lanes.*/
    for (int i = 0; i < 4; i++) {

        struct lane* lanei = &isection.lanes[i];
        pthread_mutex_init( &(lanei->lock), NULL);
        pthread_cond_init( &(lanei->consumer_cv), NULL);
        pthread_cond_init( &(lanei->producer_cv), NULL);
        lanei->in_cars = NULL;
        lanei->out_cars = NULL;
        lanei->inc = 0;
        lanei->passed = 0;
        /*Initialize buffer*/
        lanei->buffer = malloc(sizeof(struct car* ) * LANE_LENGTH);
        int k;
        for(k = 0; k < LANE_LENGTH; k++){
            lanei->buffer[k] = NULL;
        } 
        lanei->head = 0;
        lanei->tail = -1;
        lanei->capacity = LANE_LENGTH;
        lanei->in_buf = 0;

    }
}

/**
 * TODO: Fill in this function
 *
 * Populates the corresponding lanes with cars as room becomes
 * available. Ensure to notify the cross thread as new cars are
 * added to the lanes.
 * 
 */
void * car_arrive(void * arg) {
    struct lane * l = arg;

    while (1) {

        /* Lock for lanes[i] mutex*/
        pthread_mutex_lock( &(l-> lock));

        /* Terminates if there is no more cars to enter. */
        if (l-> in_cars == NULL) {

            pthread_mutex_unlock( &(l-> lock));
            break;

        }

        /* Wait until there is capacity available for the next car.*/
        while (l-> in_buf == LANE_LENGTH) {

            pthread_cond_wait( &(l-> producer_cv), &(l-> lock));

        }

        /* There is space available, add the car from list. */
        /* Add the in_cars.head into buffer.tail */
        l-> tail += 1;
        if ((l-> tail) >= (l-> capacity)) {

            l-> tail -= LANE_LENGTH;

        }
        l->buffer[l->tail] = l->in_cars;
        l -> in_cars = l -> in_cars -> next;
        l-> in_buf += 1;
        pthread_cond_signal( &(l-> consumer_cv));
        pthread_mutex_unlock( &(l-> lock));

    }

    return NULL;
}

/**
 * TODO: Fill in this function
 *
 * Moves cars from a single lanes across the intersection. Cars
 * crossing the intersection must abide the rules of the road
 * and cross along the correct path. Ensure to notify the
 * arrival thread as room becomes available in the lanes.
 *
 * Note: After crossing the intersection the car should be added
 * to the out_cars list of the lanes that corresponds to the car's
 * out_dir. Do not free the cars!
 *
 * 
 * Note: For testing purposes, each car which gets to cross the 
 * intersection should print the following three numbers on a 
 * new line, separated by spaces:
 *  - the car's 'in' direction, 'out' direction, and id.
 * 
 * You may add other print statements, but in the end, please 
 * make sure to clear any prints other than the one specified above, 
 * before submitting your final code. 
 */
void * car_cross(void * arg) {
    struct lane * l = arg;

    while (1) {

        /* Lock for lanes[i] mutex*/
        pthread_mutex_lock( &(l-> lock));
		
        if (l-> in_cars == NULL && l->passed == l->inc) {

            pthread_mutex_unlock( &(l-> lock));
            break;

        }

        /* Wait until there is car in the buffer.*/
        while (l-> in_buf == 0) {

            pthread_cond_wait( &(l-> consumer_cv), &(l-> lock));

        }

        /*When there is car in the buffer, try to remove cars from buffer and put into out_cars. */
        struct car *ready_to_go = (l-> buffer)[l->head];
        int * path = compute_path(ready_to_go-> in_dir, ready_to_go-> out_dir);
        if(path == NULL){

            pthread_mutex_unlock( &(l-> lock));
            exit(1);

	}

        /*Inquire the quad lock. */
        for (int i = 0; i < 3; i++) {

            if (path[i] != -1) {
                pthread_mutex_lock(&(isection.quad[path[i]-1]));
            }

        }

        /* After having aquired lock to the intersection, remove car from list. */
        /* Try to insert the node into the in_dir's out_cars. */
		
        struct lane *targetlane = &((isection.lanes)[ready_to_go->out_dir]);
        ready_to_go->next = targetlane-> out_cars;
        targetlane-> out_cars = ready_to_go; 
        printf("%d %d %d\n", (int) ready_to_go->in_dir,  (int)ready_to_go->out_dir, (int)ready_to_go->id);

        /* Now a car has finished crossing the intersection */
        for (int j = 2; j >= 0; j--) {

            if (path[j] != -1) {
                /*release the quad lock. */
                pthread_mutex_unlock( &(isection.quad[path[j]-1]));
            }

        }

        l-> head += 1;
        if ((l-> head) >= (l-> capacity)) {
            l-> head -= LANE_LENGTH;
        }   
        free(path);
        l-> passed += 1;
        l-> in_buf -= 1;
        pthread_cond_signal( &(l->producer_cv));
        pthread_mutex_unlock( &(l->lock));

    }

    free(l->buffer);
    return NULL;
}

/**
 * TODO: Fill in this function
 *
 * Given a car's in_dir and out_dir return a sorted 
 * list of the quadrants the car will pass through.
 * 
 */
int *compute_path(enum direction in_dir, enum direction out_dir) {
    int *path = malloc(sizeof(int)*3);
    int i;
    for(i = 0; i < 3; i++){
        path[i] = -1;
    } 

    if(in_dir == NORTH){

       if(out_dir == NORTH){

           free(path);
           printf("u-turn!\n");
           return NULL;

       }else if(out_dir == SOUTH){

           path[0] = 2;
           path[1] = 3;
           path[2] = -1;

       }else if(out_dir == WEST){

           path[0] = 2;
           path[1] = -1;
           path[2] = -1;       

       }else if(out_dir == EAST){

           path[0] = 2;
           path[1] = 3;
           path[2] = 4;

       }else{

           free(path);
           printf("invalid out_dir!\n");
           return NULL;   

       }

    }else if(in_dir == SOUTH){

       if(out_dir == NORTH){

           path[0] = 1;
           path[1] = 4;
           path[2] = -1; 

       }else if(out_dir == SOUTH){

           free(path);
           printf("u-turn!\n");
           return NULL;

       }else if(out_dir == WEST){

           path[0] = 1;
           path[1] = 2;
           path[2] = 4;       

       }else if(out_dir == EAST){

           path[0] = 4;
           path[1] = -1;
           path[2] = -1;

       }else{

           free(path);
           printf("invalid out_dir!\n");
           return NULL;  

       }
    }else if(in_dir == WEST){

       if(out_dir == NORTH){

           path[0] = 1;
           path[1] = 3;
           path[2] = 4; 

       }else if(out_dir == SOUTH){

           path[0] = 3;
           path[1] = -1;
           path[2] = -1; 

       }else if(out_dir == WEST){

           free(path);
           printf("u-turn!\n");
           return NULL;    

       }else if(out_dir == EAST){

           path[0] = 3;
           path[1] = 4;
           path[2] = -1; 

       }else{

           free(path);
           printf("invalid out_dir!\n");
           return NULL;  

       }

    }else if(in_dir == EAST){

       if(out_dir == NORTH){

           path[0] = 1;
           path[1] = -1;
           path[2] = -1; 

       }else if(out_dir == SOUTH){

           path[0] = 1;
           path[1] = 2;
           path[2] = 3; 

       }else if(out_dir == WEST){

           path[0] = 1;
           path[1] = 2;
           path[2] = -1;    

       }else if(out_dir == EAST){

           free(path);
           printf("u-turn!\n");
           return NULL; 

       }else{

           free(path);
           printf("invalid out_dir!\n");
           return NULL;  

       }

    }else{

        free(path);
        printf("invalid in_dir!\n");
        return NULL;
 
   }

    return path;
}
