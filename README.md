This program allows you to intercept keys and modify them before they are processed by your os. 

It does this by creating a virtual keyboard, and ignoring the real keyboard, and forwarding keys to the virtual keyboard instead. 

As of right now it only works on linux.

the current state of this project is that the idea is right but the way the mappings are enabled needs work

# mappings

## empty

```
-------------------------------------------------------------
|...|   |.. |.. |.. |.. | |.. |.. |.. |.. | |.. |...|...|...|
| . | . | . | . | . | . | . | . | . | . | . | - | . |.......|
|... | . | . | . | . | . | . | . | . | . | . | . | . |  .   |
|.... | . | . | . | . | . | . | . | . | . | . | . |    .....|
|...... | . | . | . | . | . | . | . | . | . | . |     ......|
|....|....|....|                       |....| .. |....|.... |
-------------------------------------------------------------
```

## standard layout
```
-------------------------------------------------------------
|esc|   |f1 |f2 |f3 |f4 | |f5 |f6 |f7 |f8 | |f9 |f10|f11|f12|
| ` | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 0 | - | = |backspa|
|tab | q | w | e | r | t | y | u | i | o | p | [ | ] |  \   |
|caps | a | s | d | f | g | h | j | k | l | ; | ' |    enter|
|lshift | z | x | c | v | b | n | m | , | . | / |     rshift|
|lctr|spec|lalt|                       |ralt| fn |menu|rctr |
-------------------------------------------------------------
```

## homesick

![homesick mapping](assets/homesick_mapping.webp)

```
-------------------------------------------------------------
|...|   |.. |.. |.. |.. | |.. |.. |.. |.. | |.. |...|...|...|
| . | . | . | . | . | . | . | . | . | . | . | - | . |.......|
|... |tab| . | . | . | . | . |bac| [ | ] | \ | . | . |  .   |
|.... |cps| . | . | . | . | . | . | . | ' |ent| . |    .....|
|...... |shf|ctr|spe|alt| . | . |fn |alt|ctr|shf|     ......|
|....|....|....|                       |....| .. |....|.... |
-------------------------------------------------------------
```



## number_pulldown


```
-------------------------------------------------------------
|...|   |.. |.. |.. |.. | |.. |.. |.. |.. | |.. |...|...|...|
| . | . | . | . | . | . | . | . | . | . | . | - | . |.......|
|... | ! | @ | # | $ | % | ^ | & | * | ( | ) | . | . |  .   |
|.... | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 0 | . |    .....|
|...... | . | . | . | . | . | . | . | . | . | . |     ......|
|....|....|....|                       |....| .. |....|.... |
-------------------------------------------------------------
```

## programming


```
-------------------------------------------------------------
|...|   |.. |.. |.. |.. | |.. |.. |.. |.. | |.. |...|...|...|
| . | . | . | . | . | . | . | . | . | . | . | - | . |.......|
|... | & | _ | = | . | . | . | + | - | * | / | . | . |  .   |
|.... | { | < | [ | ( | . | . | ) | ] | > | } | . |    .....|
|...... | . | : | . | . | . | . | . | . | . | . |     ......|
|....|....|....|                       |....| .. |....|.... |
-------------------------------------------------------------
```

## vim_arrows

```
-------------------------------------------------------------
|...|   |.. |.. |.. |.. | |.. |.. |.. |.. | |.. |...|...|...|
| . | . | . | . | . | . | . | . | . | . | . | - | . |.......|
|... | . | . | . | . | . | . | . | . | . | . | . | . |  .   |
|.... | . | . | . | . | . | . | . | . | . | . | . |    .....|
|...... | . | . | . | . | . | . | . | . | . | . |     ......|
|....|....|....|                       |....| .. |....|.... |
-------------------------------------------------------------
```


## todo

what if pressing space quickly after the mapping key enables it, so the map key for homesick is space in regular mode, and the second space activates it. If we carry that logic into homsick mapping and f is the mapping key for the numbers to come down, then we'd have to press space quickly after the mapping to enable it so space space f space would enable it. In order to do this we could add extra "virtual keys" which are not real keys, but act like them so then when the number mapping key is pressed and we let go of space it's still held down and then when space is pressed again it will then activate 

THere should be a dropout mode, where if you need to type something like 123

There should also be a stacked mapping mode, so you can enable homesick, and then enable a different mapping which will clobber keys in the current mapping but leave the other keys still active, and then pop that mapping off to get back to homsick and vise versa.


pressing shift is an interesting thought because in its own way it simply just remaps keys to other keys on the keyboard, in order to be able to press shift and any key simulateously we know that we can use the opposite hand to press the key, this is an important concept we need to use for ourselves you can create your own modifier keys as long as it exists symetrically on either side of the keyboard.

Another intersting idea is that with shift we know how that modifies keys because for a and A we understand they are the same concept but one is capitalized, for the number keys we don't have the same context, so we literally just have to bake that into our brain which is fine. With the generalized shift idea it mainly pulls in keyboard shapes to other places so that it's easier to type. Also the root mapping key space space couldn't have used the general shift logic because that would've required remapping an actual key on the keyboard first, so the space space is the entry way in and once you're in then we can remap keys to shift things. Actually now that i think about it could we map space-j to the right generalized shift and space f to the left generalized shift? idk space space is also fine i think.

So to generalize this in homesick mode we give f and j special meaning, they are left and right activators so you preface a left hand activator with j and a right hand activator with f. for example suppose we say that s and l are the left and right activators of the homerow numbers so you press space space f s and then use your right hand on the home row to select a number, and vise versa for the left, additionally we can do the same for bracket types we can set d and k to be the left and right activators for brakcets and then you press space space j k and then select a bracket type eg f maps to (, d maps to [ and s maps to {, so if we wanted to type out {} we can do space space js fl.

there should also be an easy pass off mode, where if you have the left activator pressed and then you do the right activator it will move over so that you don't have to do some reactivation process again.
