# Original Design Proposal

This document records the original, unimplemented design proposal for UniversalKeyRemapper. It is reference material and does not describe a working implementation.

现在我要用cpp开发一款鼠标映射程序exe
任务情景是有的游戏我改不了键位,我额外运行一个管理员权限的程序

编译器是g++,不要使用任何额外的库
程序KeyRemapper在src文件夹,编译和运行两个指令写成bat在script文件夹
src中包含xxxxx.krm用文本记录着按键映射的内容


要求的功能和细节如下
.krm中按照如下格式记录,注释说明了作用:
```

//  表示注释,忽视后面所有的内容
/*
    表示一段注释区域
    忽视所有的空格换行等符号,以';'作为语句的结束符
    "(,)"用来代表事件或者状态,尖括号中左边是按键,右边是'-'或'^'分别表示按下和松开,'\'表示切换
    "(Key,-)"在描述源时表达Key在被按下的状态,而在描述目标时表达Key被按下这一事件
    '='的左边是源,右边是映射目标,表达定义为,映射之间不会递归的相互映射,使得源条件成立的最后一个输入事件会被吞没
    源是由一个或多个按键状态组成,从左到右依次检查是否成立,只有前面的状态成立时才会判断后面的状态是否成立
    在源被满足后会触发目标事件,后源检测进入冷却期,只有当源不被满足后再次满足才可以重新触发
    按键状态被"||"包裹表达为其全排列
    "<>"放置在源中,内容都是以','分割,内部放置状态,表达其中之一
    '&'连接同时触发的事件(实际上是很短的时间先后触法)
*/
(A, -) = (Mouse_LButton, ^);        //按下A映射为鼠标左键松开
|(Tab, -) (Shift, -)| <(A, -), (B, -)> <(C, -), (D, -)> = (Ctrl, -) & (Enter, -);
//可拆解为下面的(2*2*2=)8句:
(Tab, -) (Shift, -) (A, -) (C, -) = (Ctrl, -) & (Enter, -);
(Shift, -) (Tab, -) (A, -) (C, -) = (Ctrl, -) & (Enter, -);
(Tab, -) (Shift, -) (B, -) (C, -) = (Ctrl, -) & (Enter, -);
(Shift, -) (Tab, -) (B, -) (C, -) = (Ctrl, -) & (Enter, -); 
(Tab, -) (Shift, -) (A, -) (D, -) = (Ctrl, -) & (Enter, -);
(Shift, -) (Tab, -) (A, -) (D, -) = (Ctrl, -) & (Enter, -);
(Tab, -) (Shift, -) (B, -) (D, -) = (Ctrl, -) & (Enter, -);
(Shift, -) (Tab, -) (B, -) (D, -) = (Ctrl, -) & (Enter, -); 
//最后一句意为依次按下Shift,Tab,B,D会触发E被按下,停顿10ms,Enter被按下,停顿100ms,F被按下

//  ':'语句"源1 : 源2 = 目标1 : 目标2 : 目标3"表达在源1触发成立时执行目标1,然后循环执行目标2,直到源2触发成立,执行目标3
//  ':'语句的目标可以缺省
//  "{}"定义一个子作用域,'{'放在源的中间或者是'='右侧,前者是表达公共按键条件,后者是表达同一源映射为多个动作
//  '{}'语句禁止在':'语句中包含':'地使用
//  "[]"也是事件,表达顺序执行方括号内的事件,如果是数字,则表达暂停时间(单位ms),如果缺省,则暂停默认间隔时间
(Ctrl,-)(V, -) : (V,^) = :[(V,-), (V,^)]: ;  //现在按下Ctrl后长按V可以连续不断的粘贴
(Ctrl, -){
    (V, -) = (C, -); //将Ctrl+V生效时触发C被按下,导致实际生效Ctrl+C
    (C, -) = (V, -); //将Ctrl+C生效时触发V被按下,导致实际生效Ctrl+V
}
(B, -) = {
    (C, -);
    [100, (Mouse_LButton,-), (Mouse_LButton,^)];
    [300, (Mouse_LButton,-), (Mouse_LButton,^), (C, ^)];
}

//  鼠标的状态和事件是特殊的,有Mouse_Move和Mouse_Wheel
//  '~'或'+~'表示无穷大的正值,'-~'表示无穷大的负值
//  (Mouse_Move, -, 100, +~)表示状态鼠标向右移动的速度大于100(单位1%屏幕宽度/0.1s)
//  (Mouse_Move, |, -5.1, 10.5)表示状态鼠标向下移动的速度大于-5.1小于10.5(单位1%屏幕高度/0.1s)
//  (Mouse_Move, \, -~, 200)表示状态鼠标移动的速度小于200(单位1%屏幕对角线长度/0.1s)
//  此处正数的'+'可以省略
//  不可以对鼠标位移和滚轮动量直接监听,只监听速度
//  '~'或"+~"在所表示的数值集合有上确界时表示其上确界,"-~"在所表示的数值集合有下确界时表示其下确界
//  (Mouse_Move, -, -~, 90)表示状态鼠标横向坐标间于屏幕宽度的0%和90%之间
//  '!'对紧接着的状态取非
//  !(Mouse_Move, |, 40, 50)表示状态鼠标横向坐标不间于屏幕高度的40%和50%之间
//
//  (Mouse_Move, +10, -20.5)表示事件鼠标向右移动10%个屏幕,向上移动20.5%个屏幕
//  相对移动距离的正负符号不可以缺省
//  (Mouse_Move, 10, 20.5)表示事件鼠标移动到坐标(10%屏宽,20.5%屏高)
//
//  (Mouse_Wheel, -, -1, +1)表示状态鼠标滚轮向右顿滚的速度大于介于-1和1之间(单位10鼠标滚轮单位/0.1s)
//  (Mouse_Wheel, |, -~, -1.5)表示状态鼠标滚轮向上顿滚的速度小于-1.5(单位10鼠标滚轮单位/0.1s)
//  (Mouse_Wheel, +1, -1)表示事件鼠标滚轮向右顿滚动1,向上滚动-1(单位10鼠标滚轮单位)
//!! 测试鼠标宏是否会自己循环触发自己
|(Mouse_Move, -, 40, 60) (Mouse_Move, |, 40, 60)| : <!(Mouse_Move, -, 40, 60), !(Mouse_Move, |, 40, 60)> 
= : (Mouse_Wheel, 0, +1) & (Mouse_Move, +10, 0) : ; //当鼠标在屏幕中央指定区域内时会同时向上滚动鼠标和向右移动鼠标,每次间隔10ms,直到鼠标离开屏幕中央指定的区域;这里的"||"可加可不加,"<>"发挥了'或'的作用

//  存在默认间隔时间,用于缺省事件之间的时间间隔,其值为10(单位ms)
//  '$'定义一个数值变量,名称不允许和按键名称相同(只能由字母和下划线组成),其赋值和自增和自减都可以作为目标事件,默认初始值为10,可以使用'='修改初始值
$ t;
t = 250;
? (Mouse_LButton,-) : (Mouse_LButton,^) = (Mouse_LButton,-):[(Mouse_Move, 0, -10), t]:(Mouse_LButton,^);    //实现自动开火压枪
//  (t,100)表示赋值t为100
//  (t,-100)表示赋值t自增100
//  (t,+100)表示赋值t自减100
//  '#'定义一个状态变量,名称不允许和按键名称相同(只能由字母和下划线组成),默认状态为非触发状态,记作'^',触发状态记作'-',状态变量的用法和属性同按键
//  变量可以参与状态的检测
//  数值变量和状态变量共享一个命名空间,变量必须先定义后使用
//  '?'是用于调试的符号,在句子前添加可以进行监听,如果是定义
? # mode;
(mode,-){
    (Ctrl, -)(Tab, -) = (mode, \);
    (Up, -) = (Mouse_Move, 0, 1.5);
    (Down, -) = (Mouse_Move, 0, -1.5);
    (Right, -) = (Mouse_Move, 0, 2);
    (Left, -) = (Mouse_Move, 0, -2);
}
(mode,^){
    (Ctrl, -)(Tab, -) = (mode, \);
}
 
//  可以获取鼠标的位置信息,通过访问变量Mouse_Px和Mouse_Py进行访问,分别是横纵坐标
//  可以获取鼠标的速度信息,通过访问变量Mouse_Vx和Mouse_Vy和Mouse_V进行访问,分别是横纵速度,速度
//  可以获取鼠标滚轮的速度信息,通过访问变量Mouse_Wx和Mouse_Wy进行访问,分别是横纵速度
//  使用"``"内部可以写入'+','-','*','\','%','^','<','>','>=','<=','==','!=','!','&&','||','?:','(',')'等运算表达式求值,变量可以在其内部使用,如(var1, +`var2/100`)
? $ Py;
(Mouse_MButton, -) : (Mouse_MButton, -)  = (Py, Mouse_Py) : (Mouse_Wheel, 0, +'Mouse_Py - Py') : ;

//  "``"还可以写入控制台指令,执行外部命令,作为事件触发,如(Execute, `XXXXX.exe`)
(L,-)(O,-)(V,-)(E,-) = (Execute, `heatSymbolDisplay.exe`)

//  Pause代表暂停和开启程序生效的按键,在程序不生效模式下,程序进入菜单状态
//  CapsLk和Win及Fn等是非法按键,不允许使用
//  指令的是相互覆盖的,从前向后依次检测执行
(A,-) = (Pause, \);
|(Ctrl,-)(Shift,-)|(Esc,-) = (Pause, \);


程序通过外部参数.ini配置文件打开时处于对应配置的不生效模式,即菜单模式
不添加配置参数打开时处于无配置模式,也是菜单模式
在程序处于菜单状态时:
指令set后加.ini配置路径即可设置为对应的模式
指令clear会清除程序缓存
```



```
/*
//  字母键
A   //A键
B   //B键
...

//  数字键
1   //One
2   //Two
...


*/

```

```

{
  "Back": 0x08,          // VK_BACK
  "Tab": 0x09,           // VK_TAB
  "Enter": 0x0D,         // VK_RETURN
  "Shift": 0x10,         // VK_SHIFT
  "LShift": 0xA0,        // VK_LSHIFT
  "RShift": 0xA1,        // VK_RSHIFT
  "Ctrl": 0x11,          // VK_CONTROL
  "LCtrl": 0xA2,         // VK_LCONTROL
  "RCtrl": 0xA3,         // VK_RCONTROL
  "Alt": 0x12,           // VK_MENU
  "LAlt": 0xA4,          // VK_LMENU
  "RAlt": 0xA5,          // VK_RMENU
  "Pause": 0x13,         // VK_PAUSE
  "CapsLock": 0x14,      // VK_CAPITAL
  "NumLock": 0x90,       // VK_NUMLOCK
  "ScrollLock": 0x91,    // VK_SCROLL
  "Esc": 0x1B,           // VK_ESCAPE
  "Space": 0x20,         // VK_SPACE
  "PageUp": 0x21,        // VK_PRIOR
  "PageDown": 0x22,      // VK_NEXT
  "End": 0x23,           // VK_END
  "Home": 0x24,          // VK_HOME
  "Left": 0x25,          // VK_LEFT
  "Up": 0x26,            // VK_UP
  "Right": 0x27,         // VK_RIGHT
  "Down": 0x28,          // VK_DOWN
  "Delete": 0x2E,        // VK_DELETE
  "Insert": 0x2D,        // VK_INSERT
  "Zero": 0x30,          // '0'
  "One": 0x31,           // '1'
  "Two": 0x32,           // '2'
  "Three": 0x33,         // '3'
  "Four": 0x34,          // '4'
  "Five": 0x35,          // '5'
  "Six": 0x36,           // '6'
  "Seven": 0x37,         // '7'
  "Eight": 0x38,         // '8'
  "Nine": 0x39,          // '9'
  "A": 0x41,             // 'A'
  "B": 0x42,             // 'B'
  "C": 0x43,             // 'C'
  "D": 0x44,             // 'D'
  "E": 0x45,             // 'E'
  "F": 0x46,             // 'F'
  "G": 0x47,             // 'G'
  "H": 0x48,             // 'H'
  "I": 0x49,             // 'I'
  "J": 0x4A,             // 'J'
  "K": 0x4B,             // 'K'
  "L": 0x4C,             // 'L'
  "M": 0x4D,             // 'M'
  "N": 0x4E,             // 'N'
  "O": 0x4F,             // 'O'
  "P": 0x50,             // 'P'
  "Q": 0x51,             // 'Q'
  "R": 0x52,             // 'R'
  "S": 0x53,             // 'S'
  "T": 0x54,             // 'T'
  "U": 0x55,             // 'U'
  "V": 0x56,             // 'V'
  "W": 0x57,             // 'W'
  "X": 0x58,             // 'X'
  "Y": 0x59,             // 'Y'
  "Z": 0x5A,             // 'Z'
  "Sleep": 0x5F,         // VK_SLEEP
  "Sub_Zero": 0x60,      // VK_NUMPAD0
  "Sub_One": 0x61,       // VK_NUMPAD1
  "Sub_Two": 0x62,       // VK_NUMPAD2
  "Sub_Three": 0x63,     // VK_NUMPAD3
  "Sub_Four": 0x64,      // VK_NUMPAD4
  "Sub_Five": 0x65,      // VK_NUMPAD5
  "Sub_Six": 0x66,       // VK_NUMPAD6
  "Sub_Seven": 0x67,     // VK_NUMPAD7
  "Sub_Eight": 0x68,     // VK_NUMPAD8
  "Sub_Nine": 0x69,      // VK_NUMPAD9
  "Sub_Multiply": 0x6A,  // VK_MULTIPLY
  "Sub_Add": 0x6B,       // VK_ADD
  "Sub_Subtract": 0x6D,  // VK_SUBTRACT
  "Sub_Divide": 0x6F,    // VK_DIVIDE
  "Sub_Decimal": 0x6E,   // VK_DECIMAL
  "F1": 0x70,            // VK_F1
  "F2": 0x71,            // VK_F2
  "F3": 0x72,            // VK_F3
  "F4": 0x73,            // VK_F4
  "F5": 0x74,            // VK_F5
  "F6": 0x75,            // VK_F6
  "F7": 0x76,            // VK_F7
  "F8": 0x77,            // VK_F8
  "F9": 0x78,            // VK_F9
  "F10": 0x79,           // VK_F10
  "F11": 0x7A,           // VK_F11
  "F12": 0x7B,           // VK_F12
  "F13": 0x7C,           // VK_F13
  "F14": 0x7D,           // VK_F14
  "F15": 0x7E,           // VK_F15
  "F16": 0x7F,           // VK_F16
  "F17": 0x80,           // VK_F17
  "F18": 0x81,           // VK_F18
  "F19": 0x82,           // VK_F19
  "F20": 0x83,           // VK_F20
  "F21": 0x84,           // VK_F21
  "F22": 0x85,           // VK_F22
  "F23": 0x86,           // VK_F23
  "F24": 0x87,           // VK_F24
  "Semicolon": 0xBA,     // VK_OEM_1 (;:键)
  "Slash": 0xBF,         // VK_OEM_2 (/?键)
  "Dot": 0xC0,           // VK_OEM_3 (·~键)
  "LeftBracket": 0xDB,    // VK_OEM_4 ([{键)
  "Backslash": 0xDC,      // VK_OEM_5 (\|键)
  "RightBracket": 0xDD,   // VK_OEM_6 (]}键)
  "Quote": 0xDE,          // VK_OEM_7 ('"键)
  "Equals": 0xBB,         // VK_OEM_PLUS (=+键)
  "Subtract": 0xBD,       // VK_OEM_MINUS (-_键)
  "Comma": 0xBC,          // VK_OEM_COMMA (,<键)
  "Period": 0xBE          // VK_OEM_PERIOD (.>键)
}
