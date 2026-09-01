---
categories: general
---

## A new beginning

It's been almost 4 years since I had this idea, and I'd barely thought about it since — until recently. Some (good) changes in my life took my lab away from me, and I haven't been able to tinker with real hardware for a very long time.

That situation is still ongoing, but I hope to be able to rebuild my lab soon (how soon, that's yet undetermined, but soon enough, I hope). Because of that, all my (unfinished) projects came back to my mind, and noVa64 took a very interesting turn once I began talking to Claude about it.

In just a few weeks, that conversation has produced ~150 pages of documentation about the project. The complexity of it has grown tenfold from what I had originally imagined. The system now has a fully featured memory virtualization scheme, supporting up to 128MB of physical RAM. Yes, that much RAM is probably overkill for a computer like this, but, as I always say, this is **educational**. I want to learn as much as possible, so there are no limits.

Several graphical modes, a remote console, a filesystem of my own (NVFS), ext2 compatibility, USB devices. You name it, you got it.

What's the point of creating a computer like the noVa64? It won't be groundbreaking; late 16-bit era machines were probably better than what I can accomplish. It won't be a cheap project, so I won't expect hordes of enthusiasts to build and program for my computer. I will probably be the only one to do it. So... why?

Learning. I've been learning my whole life, and I still am. My daily job requires it, but I feel restrained there. I want to learn all the basics. A week ago, I was scratching my head over one small thing... relocatable code. How can I compile and build a 65816 program and let it run anywhere in memory? Of course, virtual memory! Any process can see the whole 24-bit address space, while the hardware takes care of mapping it, allowing for more memory than should theoretically be possible. Suddenly, a 65816 with 64MB of RAM wasn't just possible, it was a real challenge worth chasing.

And I felt good. Understanding that piece felt good. Not that I didn't already know what virtual memory was, but now I've gone a bit deeper into it.

4 years ago, I called the first phase of the project **Feature Creep Madness**, adding idea after idea of unnecessary stuff to an amateur-built computer. I thought that was over, but I've taken it much further.

So, here I am, starting over from scratch, trying to reach so much further than I ever thought I would. Just for the fun. Just for the joy.

