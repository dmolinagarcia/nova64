---
categories: general
---

## Today I did something stupid

Well, maybe not stupid. Let's call it reckless.

For the last few weeks I've been sitting down with an AI to go through the noVa64 architecture. Not to have it design the machine for me — I started this project to learn, and outsourcing the learning would defeat the whole point. The idea was much more modest: take what I already had, the block diagram, the requirements, the scattered notes on the MMU and the video path, and turn it into something I could actually read.

That worked. Then it kept working. And I kept asking.

The result is [the synthesis document]({{ '/docsV3/' | relative_url }}): more than a hundred pages, split into sheets, from the vision and the philosophy of the project down to the memory map, the power tree, the boot sequence, the embedded controller, the video and audio path, the software stack and the build plan. There are figures. There is an index. There is a printable edition, because of course there is.

A hundred pages, for a machine that, as of today, does not exist.

And here is the thing: **none of it is verified**. Not one sheet. Every single line in that document has to be reviewed, by me, against three questions:

- **Is it feasible?** Can this be built with real parts, by one hobbyist, in a spare room, with the skills I have or can realistically learn?
- **Does it make sense?** Does it fit the rest of the machine, or is it a beautiful block diagram wired to nothing?
- **Is it worth it?** Even if it works, does it earn its place in the project — its complexity, its cost, its months?

An AI will never tell you that you can't do something. Ask it for a blitter and you get a blitter. Ask it for a paging MMU with a page fault path and you get one, complete with a diagram and a boot sequence to match. It doesn't push back, it doesn't say "you will never route that", and it doesn't count the hours. That part is my job, and I hadn't done any of it while the page count kept climbing.

Back in the first post I called this **feature creep madness**. This is the industrial version.

So the document is not a plan. It's a proposal — a very well dressed, very confident proposal — and the next phase of this project is to go through it sheet by sheet and decide what survives contact with reality. Some of it will. Some of it is going to be embarrassing. I expect the review to make the document shorter, and that will be a good sign, not a bad one.

Still, I'd rather have a hundred pages to argue with than the pile of notes I had before. The 74HCT6526 taught me that writing things down is how you find out you didn't really understand them. This just found it out a lot faster than usual.

This is what happens when you let an AI run free.

Sheet by sheet, then. Starting now.

> To close the circle: this post was written entirely by AI.
