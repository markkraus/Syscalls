**Pros of Using a FIFO Queue:**

One of the notable advantages of using a FIFO queue for the global semaphore list is its simplicity and predictability. The straightforward nature of FIFO queues 
ensures that tasks waiting for semaphores are serviced in the order they arrived, promoting fairness and orderly resource allocation. This characteristic can be 
advantageous in scenarios where maintaining a sequential and predictable order of resource access is crucial. Additionally, FIFO queues are relatively easy to implement, 
providing a simple solution for managing semaphores. Let's take a system where processing applications for a job must be done in the order that they are received. 
Using a FIFO queue makes sure that these applications are fairly processed. Since a FIFO queue has a very strict nature to its implementation, the documentation for 
implementing one is quite straightforward, and is easy to see mistakes early because it works in a very logical manner.<br>

**Cons of Using a FIFO Queue:**

However, FIFO queues come with certain limitations. In scenarios with a large number of semaphores or tasks, the scalability of a FIFO queue might become an issue. 
The linear order of waiting tasks can lead to contention and performance challenges which impacts the efficiency of resource allocation whereas a hash table would 
scale without issues of runtime performance dwindling. Furthermore, searching for a specific semaphore within a FIFO queue requires a linear search, potentially resulting 
in inefficient operations as the number of semaphores grows, which can become much slower than the constant look up time of a hash table. Uneven resource utilization can 
also be a concern, as frequently requested semaphores may create backlogs, causing delays for others in the queue.<br>

**Considerations and Trade-offs:**

The choice between a FIFO queue and a hash table for the global semaphore list should be made based on the specific implementation process. While FIFO queues are suitable 
for scenarios where orderly waiting is essential, they may face challenges in terms of scalability and search efficiency. However, if a system was to remain quite small in
its overall scalability, I think a FIFO queue would pose no major issues to the user implementing it. Alternatively, hash tables offer a more dynamic and efficient 
approach, allowing for quicker searches and potentially better scalability. The implementation complexity of hash tables and the scalability of the system should be 
considered, as it might impact code maintainability and overall system performance, as well as give the user more trouble with possible collisions, etc.<br>

In conclusion, the decision between a FIFO queue and a hash table depends on the priorities of the system's design, the anticipated workload, and the desired balance
between simplicity and performance. Each data structure comes with its own set of strengths and weaknesses, and the choice should align with the specific needs of the 
system. In my opinion, I see a FIFO queue being advantageous when it is used in a rather small system that is not working with many objects. However, as a system grows, 
and more data needs to be processed, a FIFO queue does not seem like it could hold an efficient runtime, so I would use a hash table in this situation.
