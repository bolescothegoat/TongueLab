TongueLab - Slide Bone Patcher
==============================

What it does
------------
TongueLab can make a tongue slide out of the mouth and back in, instead of just popping in and out. For that the tongue mesh has to be
bound to a movable bone. Halo's tongues (the files linga1.nif .. linga10.nif from "HALOS Human HDT Tongues", Nexus 57374) are not, so
this tool makes slide-capable copies of YOUR OWN files.

  * Your original files are never changed. The copies go into a NEW mod folder.
  * Only an exact, known version of each tongue is patched. Anything else is reported and skipped. It never guesses.
    Known versions (python tonguelab_patcher.py --list):
        - Halo's HDT Tongues, the original (Nexus 57374)            <- what most people have; preferred
        - Fill Her Up / Fill Her Up Baka Edition
        - YgNord's HALOS Tongue fix for OAhegao (Nexus 53066)
    Not supported: the Skyrim LE-format copies (for example in "HDT Ahegao Tongues and Expressions"). They are refused.
  * This tool contains no part of the tongue meshes - only a short list of changed bytes (59 per tongue) - so it can be shared freely.

You need
--------
  * Python 3.8 or newer (no extra packages), and Halo's HDT Tongues installed (or one of the other known versions above).

How to use it
-------------
  1. Open a command prompt in this folder.
  2. Run it, pointing at your mod folder (Mod Organizer 2: your "mods" folder; otherwise your Skyrim "Data" folder):

         python tonguelab_patcher.py "C:\Path\To\Your\mods"

     Add --dry-run first if you want to see what it would do without writing anything.
  3. It creates a folder called "TongueLab Slide Bone Patch" here (or where you point it with --out "...").
  4. Install that folder as a mod, and put it BELOW (after) your tongue mods so its files win.

Mod Organizer 2: it is safe to run it from outside MO2 as above. Pointing it at your "mods" folder is enough.

If you have more than one known version, the first one in the list is used. To use another one: --use YgNord  (any text from its name).

If it says "SKIPPED": the tongue files it found are a different version than the ones this patch is made for (for example from another
tongue mod, or the Skyrim LE format). Nothing is written for those files.
If it says "already slide-capable": those files are the patched ones already.

Credits
-------
The tongue meshes are Halo's Tongues (HALOS Human HDT Tongues). This patch changes one bone reference and one bind transform per file,
nothing else; each patched file was checked to leave the tongue exactly where it was at rest.
