# AXE4GN GSM drivers 
Unofically modified source code of original Atcom AXE4GN drivers (http://www.atcom.cn/gsm89.html) to work with newer versions of Asterisk and  Dahdi.

Tested with Asterisk v13 and Dahdi v2.11 and Centos 7.

To install (no need to patch original Asterisk/Dahdi source code)
1) rebuild dahdi-devel package to include mising header files (or download source code)
2) build axe4gn kernel module
3) build chan_gcom asterisk module
4) follow Atcom instructions (http://www.atcom.cn/uploadfile/2014/1215/20141215040630924.pdf) to finalize setup

* No waranty of any kind, use at your own risk.

** This work have no relation Digium or Atcom company


