#ds Convert KeyMap.txt to .ini
const FILE = '../JPEGView/Config/KeyMap.txt',
    OLD_FILE = '../JPEGView/Config/KeyMap.org.txt';

const fs = require('node:fs');

//                            v-1: define-key           v-3: comment (optional)
//                                    v-2: cmd ID
const REGEX_DEF = /^#define\s+(\w+)\s+(\d+)(?:\s+\/\/\s*(.+))?/,
//                              v-2: define-key
//                   v-1: hotkey                  v-3: comment (optional)
    REGEX_HOTKEY = /^([^\s]+)\s+(\w+)(?:\s+\/\/\s*(.+))?/;

//1. Load original KeyMap.txt
fs.readFile(FILE, 'utf8', (err, kms) => {
    if (err)
    {
        console.log('Failed to find/read KeyMap.txt');
        return;
    }
    //console.log(km);

    //2. Parse mappings in old style: <define id> and <hotkey define>
    kms = kms.split(/\r?\n\r?/);
    const defs = {},
        ids = new Map();
    kms.forEach((e) => {
        let m = REGEX_DEF.exec(e);
        if (m)
        {
            const id = parseInt(m[2], 10);
            defs[m[1]] = id;
            const data = {
                def: m[1],
                comment: m[1].substring(4) //remove leading IDM/C_
            };
            ids.set(id, data);
            if (m[3])
            {
                data.comment += `: ${m[3]}`;
            }
            return;
        }
        m = REGEX_HOTKEY.exec(e);
        if (m)
        {
            const id = defs[m[2]]; //assume hotkey mappings are always below #define's block
            if (ids.has(id))
            {
                const data = ids.get(id);
                data.hotkeys = data.hotkeys || [];
                data.hotkeys.push(m[1]); //allow multiple hotkeys
                if (data.comment) //will always have comment, since we prepend old define key for info
                {
                    if (m[3])
                        data.comment += `; ${m[3]}`;
                }
                else if (m[3])
                    data.comment = m[3];
            }
        }
    });

    //3. Remap to new style: <ID hotkey>
    const idsSorted = [...ids.keys()];
    //strangely sort does a string sort instead of numeric sort!? so supply numeric comparator
    idsSorted.sort((a, b) => {
        if (a < b) return -1;
        if (a === b) return 0;
        return 1;
    });
    //console.log(ids);

    //4. Generate new KeyMap.ini
    let s = `// Refer to KeyMap-README.html for list of command IDs available and details.
// "Briefly":
// This file defines the key mappings for JPEGView
// Make sure not to corrupt it - or else these hotkeys will not work anymore!
// Mapping multiple hotkey sequences to the same command is allowed, but not multiple commands to the same sequence
//
// Developers may look in resource.h to find the IDM/C commands for these IDs,
// and MainDlg.cpp to see what each command does.

// This section contains the key map. A small number of keys is predefined and cannot be changed:
// F1 for help, Alt+F4 for exit, 0..9 for slide show
// The following keys are recognized:
// Alt, Ctrl, Shift, Esc, Return, Space, End, Home, Back, Tab, PgDn, PgUp
// Left, Right, Up, Down, Insert, Del, Plus, Minus, Mul, Div, Comma, Period, Dash, Equals
// A .. Z  F1 .. F12

// Alt+Space is Not available (Windows shortcut); Ctrl+Space also seems unavailable.

// It is also possible to bind a command to a mouse button.
// Be careful when you override the default functionality (e.g. to pan with left mouse)!
// Mouse buttons can be combined with:  Alt, Ctrl and Shift
// but not with other keys or other mouse buttons.
// The following mouse buttons are recognized:
// MouseL, MouseR, MouseM, MouseX1, MouseX2, MouseDblClk
// (Left, right, middle, extra mouse buttons, mouse left double click)

//Syntax:
//hotkey\tNumeric_Command_ID\t//[IDM name] optional description\n\n`;
    idsSorted.forEach((id) => {
        const data = ids.get(id);
        if (data && data.hotkeys)
        {
            let printCommentOnce = true;
            data.hotkeys.forEach((hotkey) => {
                if (printCommentOnce)
                {
                    if (hotkey.length >= 8)
                        s += `${hotkey}\t${id}\t//${data.comment}\n`;
                    else
                        s += `${hotkey}\t\t${id}\t//${data.comment}\n`;
                    printCommentOnce = false;
                }
                else
                {
                    if (hotkey.length >= 8)
                        s += `${hotkey}\t${id}\n`;
                    else
                        s += `${hotkey}\t\t${id}\n`;
                }
            });
        }
        else
        {
            s += `//no_hotkey\t${id}\t//${data.comment}\n`;
        }
    });
    console.log(s);

    //5. Backup old file
    fs.rename(FILE, OLD_FILE, (err) => {
        if (err)
        {
            console.log('WRN: Failed to backup original file!');
            return;
        }
        //g. Save
        fs.writeFileSync(FILE, s.replaceAll(/\n/g, '\r\n'), 'utf8');
    });
});