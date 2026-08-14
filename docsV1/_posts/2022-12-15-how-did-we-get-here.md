---
categories: general
---

## How did we get here?

And also, who am i? And what was I thinking? I suppose those are very valid questions. I'm a child of the 80's. I grew up with, among many other things, an 8-bit computer. I enjoyed that machine a lot. Played games, did homework, and, at some point, learn to program in BASIC and then in ML. Around 4 decades later, I felt the need to own a Commodore computer again. And with that... a new interest was born.

For the last 4 years, I've been involved on a [project to recreate the MOS6526](https://github.com/dmolinagarcia/74HCT6526), used in many Commodore computers and disk drives during the 80's. One key tool for this project has been a [custom 6502 computer](https://github.com/dmolinagarcia/SBC6526) to test and debug my design.

As of now (December 2022) this is still ongoing, but getting closer to completion. Still, probably no less than a year from now, but closer anyway. And I needed some new project to feed my brain.

Back in 2018 when I started the 74HCT6526 project, I had no idea. About anything. Nothing at all. Just some very basic knowledge on boolean logic adquired 20 years ago in college, but my professional career, even though it's in the IT world, is very far away from the hardware. I needed a similar project. Completely uncharted territory. So, the idea to create a new computer from scratch pop up in my head barely two weeks ago.

This wasn't a simple SBC. This needed to be a full fledged computer. Something I could really use. A laptop it is! And I went into something I've called **feature creep madness** phase. The [list of requirements](https://dmolinagarcia.github.io/nova64/requirements) grew and grew until I felt "Ok, this is something cool". In a laptop form factor, I'll be needing video, keyboard, and mouse(trackpad) support. USB for additional storage, and/or external keyboard and mouse. Vast amounts of RAM, dedicated video... As I said... absolute madness. This is pretty much something like in the Amiga/Atari ST ballpark. Way beyond my capabilities. I have no use in mind for this computer. I don't have any idea how to implement any of that. I know I want no off-the-shelf solutions. This means no already existing video core for a FPGA, or audio, or anything.

Similar to the 74HCT6526, this is about the journey, about how I can learn absolutely everything I need to build this machine. The main reason behind all the requirements is to learn. I want to learn how the 65816 works. I want to learn about video and audio generation, and so on.

FPGAs and/or CPLDs are allowed here. This does not pretend to be period-accurate. Of course, no deadline, as I said, the focus is not about the destination, but about the journey.

A github repo is already in place and I intend to document everything on this blog.

The name, nova, is the result of a brainstorming session with ChatGPT, which convinced when it related it to a Supernova. The 64 is an homage to the Commodore 64, as it induced on me the great interest in computers that has shaped my life.

![noVa64 logo](https://raw.githubusercontent.com/dmolinagarcia/nova64/main/docs/img/logo_nova64_big.png)

I hope you spot the wink to Commodore on the logo. :D

I don't expect many updates in the short time, but I wanted to make the project public, to really feel it has already started.

Welcome aboard!
