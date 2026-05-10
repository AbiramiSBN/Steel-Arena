#if defined(_WIN32)
  // Keep Windows headers from defining names that collide with raylib
  // (Rectangle, DrawText, CloseWindow, ShowCursor, etc.).
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
  #define NOMINMAX
  #endif
  #ifndef NOGDI
  #define NOGDI
  #endif
  #ifndef NOUSER
  #define NOUSER
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
#endif

#include "raylib.h"
#include "raymath.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
  using SocketHandle = SOCKET;
  static void netInit(){ static bool done=false; if(!done){ WSADATA wsa; WSAStartup(MAKEWORD(2,2), &wsa); done=true; } }
  static void netClose(SocketHandle s){ closesocket(s); }
  static void setNonBlocking(SocketHandle s){ u_long mode=1; ioctlsocket(s, FIONBIO, &mode); }
#else
  #include <arpa/inet.h>
  #include <fcntl.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>
  using SocketHandle = int;
  static void netInit(){}
  static void netClose(SocketHandle s){ if(s>=0) close(s); }
  static void setNonBlocking(SocketHandle s){ int flags=fcntl(s,F_GETFL,0); fcntl(s,F_SETFL,flags|O_NONBLOCK); }
  #define INVALID_SOCKET (-1)
  #define SOCKET_ERROR (-1)
#endif

// Steel Arena Local v18
// No engine. Fully local/offline. C++17 + raylib. Made by y8tireu.
// v17 removes shop/loadout subtitle tips and fixes cramped tab/header overlap.

struct Gun { const char* name; int price; int damage; float cooldown; float spread; int mag; float reload; float speed; Color color; const char* desc; };
struct Melee { const char* name; int price; int damage; float range; float cooldown; Color color; const char* desc; };
struct Utility { const char* name; int price; float cooldown; Color color; const char* desc; };
struct Level { std::string name; std::string desc; int bots; int target; float skill; Color floor; Color accent; int layout; };
struct Save { int coins=600,wins=0,losses=0,kills=0,deaths=0,highestLevel=1; int primaryMask=(1<<7),secondaryMask=(1<<6),meleeMask=(1<<5),utilityMask=(1<<6); int primary=7,secondary=6,melee=5,utility=6; };
struct Loadout { int primary=7, secondary=6, melee=5, utility=6; };
struct Bot { Vector3 pos{}; float hp=100,maxhp=100,shot=0,respawn=0,stun=0,freeze=0; bool alive=true; bool dummy=false; };
struct Bullet { Vector3 pos{},vel{}; float life=0; int damage=0; bool player=false; int kind=0; float radius=0.1f; };
struct Particle { Vector3 pos{},vel{}; float life=0; Color color{}; };
struct Pickup { Vector3 pos{}; int type=0; bool active=true; };
struct Mine { Vector3 pos{}; float timer=0; float radius=3; int type=0; };

// LAN packet state shared between two devices.
struct RemoteState { Vector3 pos{0,.9f,10}; float yaw=0,pitch=0,hp=100; int fireSeq=0,hitSeq=0,hitDmg=0; int p=7,s=6,m=5,u=6; bool valid=false; double lastSeen=0; };

class LanSession {
public:
    bool active=false, hosting=false, connected=false;
    int code=0; unsigned short port=0; std::string status="Offline";
    SocketHandle sock=INVALID_SOCKET; sockaddr_in peer{}; bool hasPeer=false; double lastBroadcast=0;

    ~LanSession(){ close(); }
    void close(){ if(sock!=INVALID_SOCKET){ netClose(sock); sock=INVALID_SOCKET; } active=false; connected=false; hasPeer=false; status="Offline"; }
    bool openSocket(unsigned short bindPort){
        netInit(); close();
        sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if(sock==INVALID_SOCKET){ status="Could not create UDP socket"; return false; }
        int yes=1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
        setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (const char*)&yes, sizeof(yes));
        sockaddr_in addr{}; addr.sin_family=AF_INET; addr.sin_addr.s_addr=INADDR_ANY; addr.sin_port=htons(bindPort);
        if(bind(sock, (sockaddr*)&addr, sizeof(addr))==SOCKET_ERROR){ status="Could not bind UDP port. Try another code."; close(); return false; }
        setNonBlocking(sock); active=true; return true;
    }
    void host(int c){ code=c; port=(unsigned short)(42000 + code); hosting=true; if(openSocket(port)) status=TextFormat("Hosting LAN code %04d. Waiting for player...", code); }
    void join(int c){ code=c; port=0; hosting=false; if(openSocket(0)) status=TextFormat("Searching LAN for code %04d...", code); }
    void updateDiscovery(){
        if(!active || connected) return;
        char buf[512]; sockaddr_in from{};
#if defined(_WIN32)
        int fromLen=sizeof(from);
#else
        socklen_t fromLen=sizeof(from);
#endif
        while(true){
            int n=recvfrom(sock, buf, sizeof(buf)-1, 0, (sockaddr*)&from, &fromLen);
            if(n<=0) break; buf[n]=0; std::string msg(buf);
            if(hosting){
                std::string want = TextFormat("DISCOVER %04d", code);
                if(msg.find(want)==0){ peer=from; hasPeer=true; connected=true; const char* ok=TextFormat("ACCEPT %04d", code); sendto(sock, ok, (int)strlen(ok), 0, (sockaddr*)&peer, sizeof(peer)); status="Connected! Press Enter to start LAN match."; }
            } else {
                std::string ok = TextFormat("ACCEPT %04d", code);
                if(msg.find(ok)==0){ peer=from; hasPeer=true; connected=true; status="Connected! Starting LAN match..."; }
            }
        }
        if(!hosting && GetTime()-lastBroadcast>.65){
            lastBroadcast=GetTime();
            sockaddr_in b{}; b.sin_family=AF_INET; b.sin_port=htons((unsigned short)(42000+code)); b.sin_addr.s_addr=inet_addr("255.255.255.255");
            std::string msg=TextFormat("DISCOVER %04d", code);
            sendto(sock, msg.c_str(), (int)msg.size(), 0, (sockaddr*)&b, sizeof(b));
        }
    }
    void sendState(const std::string& msg){ if(active&&connected&&hasPeer) sendto(sock, msg.c_str(), (int)msg.size(), 0, (sockaddr*)&peer, sizeof(peer)); }
    bool recvMessage(std::string& out){
        if(!active) return false; char buf[1024]; sockaddr_in from{};
#if defined(_WIN32)
        int fromLen=sizeof(from);
#else
        socklen_t fromLen=sizeof(from);
#endif
        int n=recvfrom(sock, buf, sizeof(buf)-1, 0, (sockaddr*)&from, &fromLen);
        if(n<=0) return false; buf[n]=0; out=buf; if(!hasPeer){peer=from;hasPeer=true;} return true;
    }
};

// Weapons requested by the user.
static const std::vector<Gun> PrimaryWeapons = {
 {"Distortion",700,34,0.16f,0.018f,30,1.5f,58,PURPLE,"Stable rifle that bends shots with high velocity."},{"Permafrost",650,26,0.14f,0.020f,32,1.6f,52,SKYBLUE,"Cold rifle. Hits briefly slow bots."},{"Energy Rifle",500,29,0.13f,0.024f,35,1.4f,55,LIME,"Clean all-around energy primary."},{"Flamethrower",600,8,0.035f,0.090f,90,2.2f,24,ORANGE,"Short range stream with rapid damage."},{"Grenade Launcher",800,72,0.75f,0.035f,6,2.4f,25,ORANGE,"Explosive primary, great for crowds."},{"Minigun",900,12,0.045f,0.060f,120,3.2f,42,GRAY,"Huge magazine and heavy spread."},{"Paintball Gun",180,17,0.11f,0.045f,50,1.7f,38,MAGENTA,"Cheap spam weapon. Colorful and forgiving."},{"Assault Rifle",0,24,0.12f,0.026f,30,1.25f,48,GREEN,"Default primary. Balanced and reliable."},{"Bow",250,40,0.55f,0.015f,1,0.55f,44,BROWN,"Precise single shot. No big magazine."},{"Burst Rifle",320,18,0.075f,0.034f,36,1.5f,50,BLUE,"Fast tap burst rifle."},{"Crossbow",380,55,0.70f,0.010f,1,0.8f,46,MAROON,"Heavy single shot with strong damage."},{"Gunblade",520,27,0.16f,0.025f,24,1.4f,44,GOLD,"Rifle with strong melee feel."},{"RPG",1000,115,1.15f,0.020f,1,2.4f,22,RED,"Big explosion. Slow reload."},{"Shotgun",220,13,0.65f,0.17f,8,1.8f,36,ORANGE,"Multi-pellet close-range damage."},{"Sniper",750,90,0.95f,0.004f,5,2.2f,70,SKYBLUE,"Long-range precision rifle."},{"Scepter",850,45,0.38f,0.020f,12,1.9f,40,VIOLET,"Magic-style projectile weapon."}
};
static const std::vector<Gun> SecondaryWeapons = {
 {"Warper",420,28,0.28f,0.024f,10,1.5f,52,PURPLE,"Sidearm that rewards accurate shots."},{"Energy Pistols",360,16,0.075f,0.045f,36,1.4f,48,SKYBLUE,"Fast dual energy pistols."},{"Exogun",500,38,0.35f,0.018f,8,1.7f,54,GOLD,"High-tech heavy pistol."},{"Slingshot",120,22,0.24f,0.05f,20,1.0f,34,BROWN,"Cheap and fun projectile sidearm."},{"Daggers",220,12,0.055f,0.06f,24,1.1f,36,LIGHTGRAY,"Thrown dagger sidearm."},{"Flare Gun",300,40,0.55f,0.04f,1,1.3f,30,ORANGE,"One shot flare with splash damage."},{"Handgun",0,20,0.20f,0.032f,14,1.05f,44,LIGHTGRAY,"Default secondary."},{"Revolver",260,44,0.42f,0.018f,6,1.4f,48,MAROON,"Slow, accurate, strong."},{"Shorty",240,11,0.58f,0.16f,2,1.1f,32,ORANGE,"Pocket shotgun."},{"Spray",180,9,0.04f,0.08f,60,2.0f,30,LIME,"Low damage, high spam."},{"Uzi",300,11,0.055f,0.06f,40,1.6f,38,GRAY,"Fast automatic secondary."},{"Glass Cannon",600,95,1.1f,0.006f,1,2.0f,62,RAYWHITE,"Huge damage, almost no forgiveness."}
};
static const std::vector<Melee> MeleeWeapons = {
 {"Maul",350,78,3.0f,0.85f,BROWN,"Slow heavy smash."},{"Spear",280,50,4.0f,0.62f,LIGHTGRAY,"Long melee reach."},{"Trowel",90,25,2.1f,0.30f,GRAY,"Small joke weapon, very fast."},{"Battle Axe",400,85,3.1f,0.95f,MAROON,"Big damage axe."},{"Chainsaw",620,14,2.4f,0.075f,ORANGE,"Continuous close damage."},{"Fists",0,22,1.8f,0.24f,BEIGE,"Default melee."},{"Katana",300,55,2.9f,0.45f,SKYBLUE,"Fast clean slash."},{"Knife",120,35,2.0f,0.28f,LIGHTGRAY,"Fast melee finisher."},{"Riot Shield",500,18,2.0f,0.50f,BLUE,"Weak attack, strong defense fantasy."},{"Scythe",520,70,3.5f,0.75f,PURPLE,"Wide sweeping melee."},{"Glast Shard",700,88,3.0f,0.70f,VIOLET,"Legendary crystal slash."}
};
static const std::vector<Utility> UtilityWeapons = {
 {"Grappler",250,3.0f,SKYBLUE,"Launch forward in aim direction."},{"Medkit",150,8.0f,GREEN,"Heal 50 HP."},{"Subspace Tripmine",300,7.0f,PURPLE,"Place a mine that explodes near bots."},{"Warpstone",350,5.0f,VIOLET,"Teleport forward."},{"Flashbang",220,6.0f,YELLOW,"Stun nearby bots."},{"Freeze Ray",330,5.5f,BLUE,"Freeze the closest aimed bot."},{"Grenade",0,4.5f,ORANGE,"Default utility. Throw explosive grenade."},{"Jump Pad",200,5.0f,LIME,"Boost upward."},{"Molotov",280,6.0f,RED,"Create a burning area."},{"Satchel",380,6.5f,MAROON,"Drop delayed explosive charge."},{"Smoke Grenade",200,7.0f,GRAY,"Drop smoke that makes bots miss."},{"War Horn",500,10.0f,GOLD,"Knockback nearby bots and refill stamina."},{"Elixir",420,9.0f,MAGENTA,"Heal and refill stamina."},{"RNG Dice",777,4.0f,RAYWHITE,"Random utility effect. Chaos button."}
};

enum class Screen { Loading, MainMenu, LevelSelect, Shop, Loadout, Playing, Paused, GameOver, Credits, LanMenu, LanHost, LanJoin, LanPlaying };
enum class Slot { Primary=0, Secondary=1, Melee=2, Utility=3 };
enum class Tab { Primary=0, Secondary=1, Melee=2, Utility=3 };

static float clampf(float v,float a,float b){ return std::max(a,std::min(v,b)); }
static Vector3 norm(Vector3 v){ return Vector3Length(v)<0.001f?Vector3{0,0,1}:Vector3Normalize(v); }
static Vector3 arenaClamp(Vector3 p){ p.x=clampf(p.x,-38,38); p.z=clampf(p.z,-38,38); return p; }
static float distXZ(Vector3 a,Vector3 b){ float x=a.x-b.x,z=a.z-b.z; return sqrtf(x*x+z*z); }
static bool owned(int mask,int i){ return (mask&(1<<i))!=0; }
static Rectangle R(float x,float y,float w,float h){ return Rectangle{x,y,w,h}; }
static Vector3 forwardFrom(float yaw,float pitch){ return norm({sinf(yaw)*cosf(pitch), sinf(pitch), cosf(yaw)*cosf(pitch)}); }
static void drawTextCentered(const char* text, int cx, int y, int size, Color color){ DrawText(text, cx - MeasureText(text, size)/2, y, size, color); }
static void drawTextCenteredIn(Rectangle r, const char* text, int size, Color color){ DrawText(text, (int)(r.x + r.width/2 - MeasureText(text, size)/2), (int)(r.y + r.height/2 - size/2), size, color); }

static std::string fitTextToWidth(std::string text, int maxWidth, int fontSize){
    if((int)MeasureText(text.c_str(), fontSize) <= maxWidth) return text;
    while(!text.empty() && (int)MeasureText((text + "...").c_str(), fontSize) > maxWidth) text.pop_back();
    return text + "...";
}
static void drawTextFit(const std::string& text, int x, int y, int maxWidth, int fontSize, Color color){
    DrawText(fitTextToWidth(text, maxWidth, fontSize).c_str(), x, y, fontSize, color);
}
static void drawPill(Rectangle r, const char* text, bool selected){
    Color fill = selected ? Fade(GOLD, .92f) : Fade(WHITE, .10f);
    Color edge = selected ? GOLD : Fade(WHITE, .22f);
    DrawRectangleRounded(r, .45f, 18, fill);
    DrawRectangleRoundedLines(r, .45f, 18, edge);
    drawTextCenteredIn(r, text, 18, selected ? BLACK : RAYWHITE);
}
static int itemCount(Tab t){ if(t==Tab::Primary)return (int)PrimaryWeapons.size(); if(t==Tab::Secondary)return (int)SecondaryWeapons.size(); if(t==Tab::Melee)return (int)MeleeWeapons.size(); return (int)UtilityWeapons.size(); }
static int makeLanCode(){ return 1000 + GetRandomValue(0,8999); }

static Level getLevel(int idx){
    static const char* themes[] = {"Training Yard","Pillar Court","Lava Ring","Ramp Ruins","Neon Grid","Frost Deck","Factory Maze","Sky Platform","Volcano Core","Champion Grid"};
    static const Color floors[] = {{32,38,48,255},{35,44,45,255},{48,31,30,255},{38,36,52,255},{20,25,42,255},{20,36,48,255},{45,42,35,255},{27,34,55,255},{52,24,20,255},{14,17,28,255}};
    static const Color accents[] = {SKYBLUE,GREEN,ORANGE,PURPLE,GOLD,BLUE,ORANGE,SKYBLUE,RED,GOLD};
    int t=idx%10, tier=idx/10+1; Level l; l.name=TextFormat("%s T%d",themes[t],tier); l.desc=TextFormat("Level %d: %s. Bots scale harder every level.",idx+1,themes[t]); l.bots=std::min(4+idx/3,20); l.target=8+idx; l.skill=.45f+idx*.055f; l.floor=floors[t]; l.accent=accents[t]; l.layout=t; return l;
}
static int levelCount(){ return 50; }

static void ensureSave(){
#if defined(_WIN32)
    system("if not exist save mkdir save");
#else
    system("mkdir -p save");
#endif
}
static Save loadSave(){ ensureSave(); Save s; std::ifstream in("save/account.txt"); std::string k; int v; while(in>>k>>v){ if(k=="coins")s.coins=v; else if(k=="wins")s.wins=v; else if(k=="losses")s.losses=v; else if(k=="kills")s.kills=v; else if(k=="deaths")s.deaths=v; else if(k=="highestLevel")s.highestLevel=v; else if(k=="primaryMask")s.primaryMask=v; else if(k=="secondaryMask")s.secondaryMask=v; else if(k=="meleeMask")s.meleeMask=v; else if(k=="utilityMask")s.utilityMask=v; else if(k=="primary")s.primary=v; else if(k=="secondary")s.secondary=v; else if(k=="melee")s.melee=v; else if(k=="utility")s.utility=v; } s.highestLevel=std::max(1,std::min(s.highestLevel,levelCount())); if(!owned(s.primaryMask,s.primary))s.primary=7; if(!owned(s.secondaryMask,s.secondary))s.secondary=6; if(!owned(s.meleeMask,s.melee))s.melee=5; if(!owned(s.utilityMask,s.utility))s.utility=6; return s; }
static void saveGame(const Save&s){ ensureSave(); std::ofstream o("save/account.txt"); o<<"coins "<<s.coins<<"\nwins "<<s.wins<<"\nlosses "<<s.losses<<"\nkills "<<s.kills<<"\ndeaths "<<s.deaths<<"\nhighestLevel "<<s.highestLevel<<"\nprimaryMask "<<s.primaryMask<<"\nsecondaryMask "<<s.secondaryMask<<"\nmeleeMask "<<s.meleeMask<<"\nutilityMask "<<s.utilityMask<<"\nprimary "<<s.primary<<"\nsecondary "<<s.secondary<<"\nmelee "<<s.melee<<"\nutility "<<s.utility<<"\n"; }


static Color C(int r,int g,int b,int a=255){ return Color{(unsigned char)r,(unsigned char)g,(unsigned char)b,(unsigned char)a}; }
static void DrawCircleGradientSA(float x, float y, float radius, Color inner, Color outer){
#if defined(_WIN32)
    // MSYS2/UCRT raylib uses DrawCircleGradient(int centerX, int centerY, ...).
    DrawCircleGradient((int)x, (int)y, radius, inner, outer);
#else
    // Newer raylib source builds may use DrawCircleGradient(Vector2 center, ...).
    DrawCircleGradient(Vector2{x, y}, radius, inner, outer);
#endif
}
static void drawMenuBackdrop(){
    ClearBackground(C(5,7,16));
    int w=GetScreenWidth(), h=GetScreenHeight();
    DrawRectangleGradientV(0,0,w,h,C(7,10,25),C(10,14,33));
    DrawCircleGradientSA((float)w - 170.0f, 95.0f, 260.0f, Fade(SKYBLUE,.28f), Fade(BLANK,0));
    DrawCircleGradientSA(130.0f, (float)h - 90.0f, 230.0f, Fade(PURPLE,.22f), Fade(BLANK,0));
    for(int i=0;i<70;i++){
        int x=(i*197+47)%std::max(1,w), y=(i*113+29)%std::max(1,h);
        float pulse=.25f+.25f*sinf((float)GetTime()*1.7f+i);
        DrawPixel(x,y,Fade(RAYWHITE,pulse));
    }
    DrawRectangle(0,0,w,56,Fade(BLACK,.32f));
    DrawText("STEEL ARENA LOCAL",24,17,20,Fade(RAYWHITE,.82f));
    DrawText("v18 Windows compile fix - Made by y8tireu",w-300,17,18,Fade(GOLD,.9f));
}
static void drawPanel(Rectangle r, Color tint=Color{255,255,255,255}){
    DrawRectangleRounded(r,.14f,16,Fade(tint,.075f));
    DrawRectangleRoundedLines(r,.14f,16,Fade(WHITE,.22f));
    DrawRectangleRounded(R(r.x+2,r.y+2,r.width-4,6),.8f,6,Fade(WHITE,.13f));
}
static void drawButton(Rectangle r,const char* text,bool selected){
    Color fill = selected ? GOLD : Fade(WHITE,.105f);
    Color edge = selected ? RAYWHITE : Fade(WHITE,.28f);
    if(selected){
        DrawRectangleRounded(R(r.x-6,r.y-5,r.width+12,r.height+10),.22f,16,Fade(GOLD,.15f));
        DrawRectangleRounded(R(r.x-2,r.y-2,r.width+4,r.height+4),.20f,16,Fade(GOLD,.30f));
    }
    DrawRectangleRounded(r,.18f,16,fill);
    DrawRectangleRoundedLines(r,.18f,16,edge);
    DrawRectangleRounded(R(r.x+5,r.y+5,r.width-10,5),.7f,6,selected?Fade(WHITE,.45f):Fade(WHITE,.12f));
    drawTextCenteredIn(r, text, 22, selected?BLACK:RAYWHITE);
}
static void title(const char* a,const char* b){
    drawMenuBackdrop();
    int cx = GetScreenWidth()/2;
    drawTextCentered(a,cx,76,52,GOLD);
    if(b && b[0]) drawTextCentered(b,cx,135,20,Fade(RAYWHITE,.72f));
    DrawLineEx({(float)cx-270,166},{(float)cx+270,166},3,Fade(GOLD,.55f));
}
static void titlePlain(const char* a){
    // Clean header for dense screens like Shop/Loadout/Practice: no subtitle/tip text.
    drawMenuBackdrop();
    int cx = GetScreenWidth()/2;
    drawTextCentered(a,cx,78,52,GOLD);
    DrawLineEx({(float)cx-270,145},{(float)cx+270,145},3,Fade(GOLD,.55f));
}
static void menuList(const std::vector<std::string>& items,int sel,int x,int y){
    (void)x;
    int bw = 520;
    int bx = GetScreenWidth()/2 - bw/2;
    DrawRectangleRounded(R((float)bx-24,(float)y-22,(float)bw+48,(float)items.size()*60+36),.14f,14,Fade(BLACK,.26f));
    DrawRectangleRoundedLines(R((float)bx-24,(float)y-22,(float)bw+48,(float)items.size()*60+36),.14f,14,Fade(WHITE,.16f));
    for(int i=0;i<(int)items.size();i++) drawButton(R((float)bx,(float)y+i*60,(float)bw,48),items[i].c_str(),i==sel);
}
static void drawLoading(float p){
    drawMenuBackdrop();
    float pulse=.5f+.5f*sinf((float)GetTime()*3.0f);
    DrawText("STEEL ARENA",105,106,70,Fade(GOLD,.95f));
    DrawText("LOCAL",105,174,48,Fade(SKYBLUE,.85f));
    DrawText("Building arenas, LAN code system, weapons, bots, practice range...",108,246,22,Fade(RAYWHITE,.78f));
    Rectangle card=R(105,300,870,160); drawPanel(card,SKYBLUE);
    Rectangle bg=R(140,360,790,30);
    DrawRectangleRounded(bg,.45f,24,Fade(BLACK,.45f));
    DrawRectangleRounded(R(bg.x,bg.y,bg.width*clampf(p,0,1),bg.height),.45f,24,Fade(GOLD,.95f));
    DrawRectangleRoundedLines(bg,.45f,24,Fade(WHITE,.35f));
    const char* steps[] = {"loading settings", "syncing local save", "generating arenas", "warming up bots", "charging weapons", "starting renderer"};
    int step=std::min(5,(int)(p*6));
    DrawText(TextFormat("%s... %d%%",steps[step],(int)(p*100)),140,318,24,RAYWHITE);
    DrawText("Tip: F11 toggles fullscreen. C switches first/third person. Shop/loadout uses keyboard controls.",140,414,20,Fade(SKYBLUE,.95f));
    DrawCircleGradientSA(1000.0f, 180.0f, 90.0f, Fade(GOLD,.22f+pulse*.10f), Fade(BLANK,0));
    DrawText("Made by y8tireu",105,510,22,Fade(RAYWHITE,.65f));
}
static void drawBar(int x,int y,int w,int h,float val,float max,Color col,const char* label){
    float pct=max<=0?0:clampf(val/max,0,1); Rectangle r=R((float)x,(float)y,(float)w,(float)h);
    DrawRectangleRounded(r,.35f,12,Fade(BLACK,.48f));
    DrawRectangleRounded(R(r.x,r.y,r.width*pct,r.height),.35f,12,col);
    DrawRectangleRoundedLines(r,.35f,12,Fade(WHITE,.38f));
    DrawText(TextFormat("%s %.0f/%.0f",label,val,max),x+9,y+4,18,RAYWHITE);
}
static void drawArena(int level,const std::vector<Mine>& mines,bool practice){
    Level lv=getLevel(level);
    Color floor = practice?C(24,42,33):lv.floor;
    DrawPlane({0,0,0},{86,86},floor);
    int tile=0;
    for(int x=-40;x<=40;x+=8){
        for(int z=-40;z<=40;z+=8){
            Color c = ((tile++ + level)%2)==0 ? Fade(WHITE,.035f) : Fade(BLACK,.10f);
            DrawCubeV({(float)x,.012f,(float)z},{7.6f,.02f,7.6f},c);
        }
    }
    for(int i=-40;i<=40;i+=4){
        Color grid = (i%16==0)?Fade(lv.accent,.55f):Fade(lv.accent,.18f);
        DrawCubeV({(float)i,.025f,0},{.055f,.035f,82},grid);
        DrawCubeV({0,.026f,(float)i},{82,.035f,.055f},grid);
    }
    DrawPlane({0,-.04f,0},{90,90},Fade(lv.accent,.08f));
    for(int i=0;i<4;i++){
        float z=(i<2?-42:42), x=(i%2?-42:42);
        DrawCubeV({0,2,z},{84,4,1.6f},Fade(lv.accent,.78f));
        DrawCubeV({x,2,0},{1.6f,4,84},Fade(lv.accent,.78f));
        DrawCubeV({0,4.3f,z},{84,.35f,.55f},GOLD);
        DrawCubeV({x,4.3f,0},{.55f,.35f,84},GOLD);
    }
    int layout=practice?0:lv.layout;
    for(int i=0;i<layout+5;i++){
        float x=-30+(i%5)*15; float z=-24+(i/5)*16;
        Color block=Fade(lv.accent,.62f);
        DrawCubeV({x,1.25f,z},{3.5f,2.5f,3.5f},block);
        DrawCubeV({x,2.65f,z},{4.1f,.28f,4.1f},Fade(GOLD,.75f));
        DrawCylinder({x,3.4f,z},.55f,.35f,1.4f,16,Fade(WHITE,.12f));
    }
    if(layout>=3){
        for(int i=0;i<3;i++){
            float x=-22.f+i*22; DrawCubeV({x,.45f,-12},{10,.9f,4.6f},Fade(lv.accent,.45f));
            DrawCubeV({x,.95f,-12},{9,.12f,3.8f},Fade(WHITE,.25f));
        }
    }
    if(layout>=2) for(int i=0;i<3;i++){
        float x=-20.f+i*20; DrawCubeV({x,.055f,22},{8,.11f,8},Fade(RED,.58f));
        DrawCubeV({x,.12f,22},{7,.08f,7},Fade(ORANGE,.42f));
    }
    for(auto&m:mines){ DrawSphere(m.pos,.48f,m.type?ORANGE:PURPLE); DrawCircle3D(m.pos,m.radius,{0,1,0},90,Fade(m.type?ORANGE:PURPLE,.22f)); }
}
static void drawPlayerModel(Vector3 pos, Color color, bool firstPerson){
    if(firstPerson) return;
    DrawCylinder({pos.x,pos.y+.58f,pos.z},.43f,.38f,1.15f,16,color);
    DrawSphere({pos.x,pos.y+1.35f,pos.z},.34f,Fade(color,.92f));
    DrawCubeV({pos.x,pos.y+.95f,pos.z+.42f},{.22f,.22f,.75f},Fade(RAYWHITE,.75f));
    DrawCylinder({pos.x-.36f,pos.y+.75f,pos.z},.10f,.09f,.65f,10,Fade(color,.75f));
    DrawCylinder({pos.x+.36f,pos.y+.75f,pos.z},.10f,.09f,.65f,10,Fade(color,.75f));
}
static void drawBot(const Bot& b,int level,bool dummy=false){
    if(!b.alive)return; Color base=dummy?GREEN:getLevel(level).accent; Color c=b.freeze>0?SKYBLUE:(b.stun>0?YELLOW:base);
    DrawCubeV({b.pos.x,b.pos.y+.58f,b.pos.z},{.92f,1.16f,.92f},Fade(c,.9f));
    DrawCubeV({b.pos.x,b.pos.y+1.25f,b.pos.z},{.72f,.28f,.72f},Fade(RAYWHITE,.20f));
    DrawSphere({b.pos.x,b.pos.y+1.48f,b.pos.z},.28f,RED);
    DrawCubeV({b.pos.x,b.pos.y+.95f,b.pos.z+.55f},{.22f,.20f,.9f},Fade(RED,.75f));
    float hpPct=clampf(b.hp/std::max(1.0f,b.maxhp),0,1);
    DrawCubeV({b.pos.x,b.pos.y+1.95f,b.pos.z},{1.05f*hpPct,.055f,.055f},b.dummy?GREEN:RED);
}
static void spark(std::vector<Particle>& parts,Vector3 at,Color c,int n){ for(int i=0;i<n;i++){ parts.push_back({at,{(float)GetRandomValue(-100,100)/95.0f,(float)GetRandomValue(20,140)/90.0f,(float)GetRandomValue(-100,100)/95.0f},.45f,c}); } }
static void spawnBots(std::vector<Bot>& bots,int lvl,bool practice){ bots.clear(); int n=practice?7:getLevel(lvl).bots; for(int i=0;i<n;i++){ Bot b; b.pos={(float)GetRandomValue(-320,320)/10.0f,.9f,(float)GetRandomValue(-320,320)/10.0f}; b.maxhp=practice?80:(90+lvl*7); b.hp=b.maxhp; b.dummy=practice; bots.push_back(b); } }
static void shootGun(std::vector<Bullet>& bullets,Vector3 origin,Vector3 aim,const Gun& g,bool player,bool explosive=false){ int pellets=(std::string(g.name)=="Shotgun"||std::string(g.name)=="Shorty")?7:1; for(int i=0;i<pellets;i++){ float sp=g.spread*(player?1.0f:1.5f); Vector3 dir=norm({aim.x+(float)GetRandomValue(-100,100)/100.0f*sp,aim.y+(float)GetRandomValue(-100,100)/100.0f*sp,aim.z+(float)GetRandomValue(-100,100)/100.0f*sp}); bullets.push_back({origin,Vector3Scale(dir,g.speed),3.0f,g.damage,player,explosive?1:0,explosive?.22f:.12f}); } }

static void drawHud(const Loadout& l,Slot slot,int ammoP,int ammoS,float hp,float stamina,int kills,int deaths,int level,bool fp,bool practice,bool lan=false,float remoteHp=100){
    DrawRectangleRounded(R(14,12,330,132),.12f,16,Fade(BLACK,.46f));
    DrawRectangleRoundedLines(R(14,12,330,132),.12f,16,Fade(WHITE,.18f));
    drawBar(28,26,285,24,hp,100,RED,"HP"); drawBar(28,58,285,22,stamina,100,GREEN,"STAM");
    DrawText(TextFormat("Kills %d  Deaths %d",kills,deaths),30,91,20,RAYWHITE);
    DrawText(practice?"PRACTICE RANGE":(lan?"LAN ARENA":TextFormat("LEVEL %d / 50",level+1)),30,116,18,GOLD);
    if(lan)DrawText(TextFormat("Remote HP %.0f",remoteHp),190,116,18,SKYBLUE);
    int sx=GetScreenWidth()-392, sy=GetScreenHeight()-104; const char* names[4]={PrimaryWeapons[l.primary].name,SecondaryWeapons[l.secondary].name,MeleeWeapons[l.melee].name,UtilityWeapons[l.utility].name}; Color cols[4]={PrimaryWeapons[l.primary].color,SecondaryWeapons[l.secondary].color,MeleeWeapons[l.melee].color,UtilityWeapons[l.utility].color};
    DrawRectangleRounded(R((float)sx-14,(float)sy-15,388,94),.14f,16,Fade(BLACK,.48f));
    for(int i=0;i<4;i++){ Rectangle r=R((float)sx+i*92,(float)sy,82,68); bool active=(int)slot==i; DrawRectangleRounded(r,.18f,14,active?Fade(GOLD,.90f):Fade(WHITE,.10f)); DrawRectangleRoundedLines(r,.18f,14,active?RAYWHITE:Fade(WHITE,.22f)); DrawCircle((int)r.x+18,(int)r.y+18,10,cols[i]); DrawText(TextFormat("%d",i+1),(int)r.x+35,(int)r.y+7,18,active?BLACK:RAYWHITE); DrawText(TextSubtext(names[i],0,9),(int)r.x+9,(int)r.y+38,14,active?BLACK:RAYWHITE); }
    DrawRectangleRounded(R(GetScreenWidth()-265,14,245,34),.30f,12,Fade(BLACK,.42f)); DrawText(fp?"FIRST PERSON  [C]":"THIRD PERSON  [C]",GetScreenWidth()-252,22,18,SKYBLUE);
}

static std::string makeState(Vector3 p,float yaw,float pitch,float hp,int fireSeq,int hitSeq,int hitDmg,const Loadout& l){ return TextFormat("STATE %.3f %.3f %.3f %.3f %.3f %.1f %d %d %d %d %d %d %d",p.x,p.y,p.z,yaw,pitch,hp,fireSeq,hitSeq,hitDmg,l.primary,l.secondary,l.melee,l.utility); }
static bool parseState(const std::string& m,RemoteState& r){ char tag[16]; float x,y,z,yaw,pitch,hp; int fs,hs,hd,p,s,me,u; if(sscanf(m.c_str(),"%15s %f %f %f %f %f %f %d %d %d %d %d %d %d",tag,&x,&y,&z,&yaw,&pitch,&hp,&fs,&hs,&hd,&p,&s,&me,&u)==14 && std::string(tag)=="STATE"){ r.pos={x,y,z}; r.yaw=yaw; r.pitch=pitch; r.hp=hp; r.fireSeq=fs; r.hitSeq=hs; r.hitDmg=hd; r.p=p; r.s=s; r.m=me; r.u=u; r.valid=true; r.lastSeen=GetTime(); return true; } return false; }

int main(){
    srand((unsigned)time(nullptr)); SetConfigFlags(FLAG_WINDOW_RESIZABLE); InitWindow(1280,720,"Steel Arena Local v16 - Made by y8tireu"); SetWindowMinSize(1180,680); SetTargetFPS(60); InitAudioDevice();
    Save save=loadSave(); Loadout play{save.primary,save.secondary,save.melee,save.utility}; Screen screen=Screen::Loading; int menuSel=0,levelSel=0,shopSel=0,loadSel=0,shopScroll=0,loadScroll=0,selectedLevel=0,joinCode=0; Tab shopTab=Tab::Primary, loadTab=Tab::Primary; bool practice=false,firstPerson=false,lanMode=false; float loading=0,yaw=0,pitch=-0.08f;
    Vector3 player{0,.9f,0},vel{0,0,0}; Camera3D cam{{0,7,-9},{0,1,0},{0,1,0},60,CAMERA_PERSPECTIVE}; std::vector<Bot> bots; std::vector<Bullet> bullets; std::vector<Particle> parts; std::vector<Pickup> pickups; std::vector<Mine> mines; Slot slot=Slot::Primary; float hp=100,stamina=100,fireCd=0,meleeCd=0,utilCd=0,reloadP=0,reloadS=0,invuln=0,shake=0; int ammoP=PrimaryWeapons[play.primary].mag,ammoS=SecondaryWeapons[play.secondary].mag,kills=0,deaths=0; LanSession lan; RemoteState remote; int fireSeq=0,remoteSeenFire=0,hitSeq=0,remoteSeenHit=0;

    auto beginLoadout=[&](bool isPractice,int lvl,bool forLan=false){ lanMode=forLan; practice=isPractice; selectedLevel=std::max(0,std::min(lvl,levelCount()-1)); play={save.primary,save.secondary,save.melee,save.utility}; loadSel=loadScroll=0; loadTab=Tab::Primary; screen=Screen::Loadout; EnableCursor(); };
    auto reset=[&](bool isPractice,int lvl,bool isLan=false){ lanMode=isLan; practice=isPractice; selectedLevel=std::max(0,std::min(lvl,levelCount()-1)); player={lan.hosting?-6.0f:6.0f,.9f,0}; vel={0,0,0}; hp=100; stamina=100; fireCd=meleeCd=utilCd=reloadP=reloadS=invuln=shake=0; kills=deaths=0; bullets.clear(); parts.clear(); pickups.clear(); mines.clear(); ammoP=PrimaryWeapons[play.primary].mag; ammoS=SecondaryWeapons[play.secondary].mag; remote={}; remote.pos={lan.hosting?6.0f:-6.0f,.9f,0}; fireSeq=remoteSeenFire=hitSeq=remoteSeenHit=0; if(!isLan)spawnBots(bots,selectedLevel,practice); else bots.clear(); DisableCursor(); screen=isLan?Screen::LanPlaying:Screen::Playing; };
    auto useShopOrLoadout=[&](bool isShop){ Tab tab=isShop?shopTab:loadTab; int sel=isShop?shopSel:loadSel; bool canUse=practice||lanMode; if(tab==Tab::Primary){ int price=PrimaryWeapons[sel].price; bool ok=owned(save.primaryMask,sel); if(isShop){ if(ok)save.primary=sel; else if(save.coins>=price){save.coins-=price;save.primaryMask|=1<<sel;save.primary=sel;} play.primary=save.primary; saveGame(save);} else { if(canUse||ok)play.primary=sel; } } else if(tab==Tab::Secondary){ int price=SecondaryWeapons[sel].price; bool ok=owned(save.secondaryMask,sel); if(isShop){ if(ok)save.secondary=sel; else if(save.coins>=price){save.coins-=price;save.secondaryMask|=1<<sel;save.secondary=sel;} play.secondary=save.secondary; saveGame(save);} else { if(canUse||ok)play.secondary=sel; } } else if(tab==Tab::Melee){ int price=MeleeWeapons[sel].price; bool ok=owned(save.meleeMask,sel); if(isShop){ if(ok)save.melee=sel; else if(save.coins>=price){save.coins-=price;save.meleeMask|=1<<sel;save.melee=sel;} play.melee=save.melee; saveGame(save);} else { if(canUse||ok)play.melee=sel; } } else { int price=UtilityWeapons[sel].price; bool ok=owned(save.utilityMask,sel); if(isShop){ if(ok)save.utility=sel; else if(save.coins>=price){save.coins-=price;save.utilityMask|=1<<sel;save.utility=sel;} play.utility=save.utility; saveGame(save);} else { if(canUse||ok)play.utility=sel; } } };

    while(!WindowShouldClose()){
        float dt=GetFrameTime(); if(IsKeyPressed(KEY_F11)){ if(!IsWindowFullscreen())SetWindowSize(GetMonitorWidth(0),GetMonitorHeight(0)); ToggleFullscreen(); }
        if(screen==Screen::Loading){ loading+=dt*.42f; if(loading>=1||IsKeyPressed(KEY_ENTER))screen=Screen::MainMenu; }
        else if(screen==Screen::MainMenu){ std::vector<std::string> items={"Start Next Level","Choose Levels","Practice Mode","Shop / Buy Items","LAN Multiplayer","Credits","Quit"}; if(IsKeyPressed(KEY_DOWN)||IsKeyPressed(KEY_S))menuSel=(menuSel+1)%items.size(); if(IsKeyPressed(KEY_UP)||IsKeyPressed(KEY_W))menuSel=(menuSel+items.size()-1)%items.size(); if(IsKeyPressed(KEY_ENTER)){ if(menuSel==0)beginLoadout(false,std::min(save.highestLevel-1,levelCount()-1)); else if(menuSel==1)screen=Screen::LevelSelect; else if(menuSel==2)beginLoadout(true,0); else if(menuSel==3){shopSel=shopScroll=0;shopTab=Tab::Primary;screen=Screen::Shop;} else if(menuSel==4){menuSel=0;screen=Screen::LanMenu;} else if(menuSel==5)screen=Screen::Credits; else break; } }
        else if(screen==Screen::LanMenu){ std::vector<std::string> items={"Host LAN Match","Join LAN Match","Back"}; if(IsKeyPressed(KEY_DOWN)||IsKeyPressed(KEY_S))menuSel=(menuSel+1)%items.size(); if(IsKeyPressed(KEY_UP)||IsKeyPressed(KEY_W))menuSel=(menuSel+items.size()-1)%items.size(); if(IsKeyPressed(KEY_ENTER)){ if(menuSel==0){ lan.host(makeLanCode()); screen=Screen::LanHost; } else if(menuSel==1){ joinCode=0; lan.close(); screen=Screen::LanJoin; } else screen=Screen::MainMenu; } if(IsKeyPressed(KEY_BACKSPACE)||IsKeyPressed(KEY_ESCAPE))screen=Screen::MainMenu; }
        else if(screen==Screen::LanHost){ lan.updateDiscovery(); if(IsKeyPressed(KEY_BACKSPACE)||IsKeyPressed(KEY_ESCAPE)){lan.close();screen=Screen::LanMenu;} if(lan.connected && IsKeyPressed(KEY_ENTER)) beginLoadout(false,0,true); }
        else if(screen==Screen::LanJoin){ for(int k=KEY_ZERO;k<=KEY_NINE;k++) if(IsKeyPressed(k) && joinCode<1000) joinCode=joinCode*10+(k-KEY_ZERO); for(int k=KEY_KP_0;k<=KEY_KP_9;k++) if(IsKeyPressed(k) && joinCode<1000) joinCode=joinCode*10+(k-KEY_KP_0); if(IsKeyPressed(KEY_BACKSPACE)) joinCode/=10; if(IsKeyPressed(KEY_ESCAPE)){lan.close();screen=Screen::LanMenu;} if(IsKeyPressed(KEY_ENTER) && joinCode>=1000){ lan.join(joinCode); } lan.updateDiscovery(); if(lan.connected) beginLoadout(false,0,true); }
        else if(screen==Screen::LevelSelect){ if(IsKeyPressed(KEY_BACKSPACE)||IsKeyPressed(KEY_ESCAPE))screen=Screen::MainMenu; if(IsKeyPressed(KEY_DOWN)||IsKeyPressed(KEY_S))levelSel=(levelSel+1)%levelCount(); if(IsKeyPressed(KEY_UP)||IsKeyPressed(KEY_W))levelSel=(levelSel+levelCount()-1)%levelCount(); if(IsKeyPressed(KEY_ENTER)&&levelSel<save.highestLevel)beginLoadout(false,levelSel); }
        else if(screen==Screen::Shop || screen==Screen::Loadout){ bool isShop=screen==Screen::Shop; Tab& tab=isShop?shopTab:loadTab; int& sel=isShop?shopSel:loadSel; int& scroll=isShop?shopScroll:loadScroll; int count=itemCount(tab); if(IsKeyPressed(KEY_BACKSPACE)||IsKeyPressed(KEY_ESCAPE)){ if(isShop)saveGame(save); screen=lanMode?Screen::LanMenu:Screen::MainMenu; } if(IsKeyPressed(KEY_RIGHT)||IsKeyPressed(KEY_D)){ tab=(Tab)(((int)tab+1)%4); sel=scroll=0; } if(IsKeyPressed(KEY_LEFT)||IsKeyPressed(KEY_A)){ tab=(Tab)(((int)tab+3)%4); sel=scroll=0; } if(IsKeyPressed(KEY_DOWN)||IsKeyPressed(KEY_S))sel=std::min(count-1,sel+1); if(IsKeyPressed(KEY_UP)||IsKeyPressed(KEY_W))sel=std::max(0,sel-1); scroll=std::max(0,std::min(sel,std::max(0,count-7))); if(IsKeyPressed(KEY_ENTER))useShopOrLoadout(isShop); if(!isShop && IsKeyPressed(KEY_F))reset(practice,selectedLevel,lanMode); }
        else if(screen==Screen::Credits){ if(IsKeyPressed(KEY_BACKSPACE)||IsKeyPressed(KEY_ESCAPE)||IsKeyPressed(KEY_ENTER))screen=Screen::MainMenu; }
        else if(screen==Screen::Paused){ if(IsKeyPressed(KEY_P)||IsKeyPressed(KEY_ENTER)){DisableCursor();screen=lanMode?Screen::LanPlaying:Screen::Playing;} if(IsKeyPressed(KEY_R))reset(practice,selectedLevel,lanMode); if(IsKeyPressed(KEY_BACKSPACE)||IsKeyPressed(KEY_ESCAPE)){EnableCursor();screen=Screen::MainMenu;} }
        else if(screen==Screen::GameOver){ if(IsKeyPressed(KEY_ENTER))beginLoadout(false,std::min(selectedLevel+1,levelCount()-1)); if(IsKeyPressed(KEY_BACKSPACE)||IsKeyPressed(KEY_ESCAPE))screen=Screen::MainMenu; }
        else if(screen==Screen::Playing || screen==Screen::LanPlaying){
            bool isLan=(screen==Screen::LanPlaying); if(IsKeyPressed(KEY_P)){EnableCursor();screen=Screen::Paused;} if(IsKeyPressed(KEY_C))firstPerson=!firstPerson; if(IsKeyPressed(KEY_ONE))slot=Slot::Primary; if(IsKeyPressed(KEY_TWO))slot=Slot::Secondary; if(IsKeyPressed(KEY_THREE))slot=Slot::Melee; if(IsKeyPressed(KEY_FOUR))slot=Slot::Utility;
            Vector2 md=GetMouseDelta(); yaw-=md.x*.003f; pitch=clampf(pitch-md.y*.0025f,-1.25f,1.05f); Vector3 aim=forwardFrom(yaw,pitch); Vector3 flat=norm({aim.x,0,aim.z}); Vector3 right=norm(Vector3CrossProduct(flat,{0,1,0})); Vector3 move{}; if(IsKeyDown(KEY_W))move=Vector3Add(move,flat); if(IsKeyDown(KEY_S))move=Vector3Subtract(move,flat); if(IsKeyDown(KEY_D))move=Vector3Add(move,right); if(IsKeyDown(KEY_A))move=Vector3Subtract(move,right); bool sprint=IsKeyDown(KEY_LEFT_SHIFT)&&stamina>0&&Vector3Length(move)>0; float speed=sprint?8.5f:5.2f; if(sprint)stamina-=28*dt; else stamina=std::min(100.0f,stamina+22*dt); if(Vector3Length(move)>0)player=arenaClamp(Vector3Add(player,Vector3Scale(norm(move),speed*dt))); if(IsKeyPressed(KEY_SPACE)&&fabs(player.y-.9f)<.05f&&stamina>12){vel.y=7.0f;stamina-=12;} if(IsKeyPressed(KEY_Q)&&stamina>25){player=arenaClamp(Vector3Add(player,Vector3Scale(flat,6.0f)));stamina-=25;} vel.y-=18*dt; player.y+=vel.y*dt; if(player.y<.9f){player.y=.9f;vel.y=0;} fireCd-=dt; meleeCd-=dt; utilCd-=dt; reloadP-=dt; reloadS-=dt; invuln-=dt;
            if(IsKeyPressed(KEY_E)){ if(slot==Slot::Primary)reloadP=PrimaryWeapons[play.primary].reload; if(slot==Slot::Secondary)reloadS=SecondaryWeapons[play.secondary].reload; } if(reloadP<=0 && ammoP<=0)ammoP=PrimaryWeapons[play.primary].mag; if(reloadS<=0 && ammoS<=0)ammoS=SecondaryWeapons[play.secondary].mag;
            bool fired=false; if(IsMouseButtonDown(MOUSE_LEFT_BUTTON)){ Vector3 origin=Vector3Add(player,{0,1.2f,0}); if(slot==Slot::Primary){auto&g=PrimaryWeapons[play.primary]; if(fireCd<=0&&ammoP>0&&reloadP<=0){shootGun(bullets,origin,aim,g,true,std::string(g.name)=="RPG"||std::string(g.name)=="Grenade Launcher"); ammoP--; fireCd=g.cooldown; fired=true;}} else if(slot==Slot::Secondary){auto&g=SecondaryWeapons[play.secondary]; if(fireCd<=0&&ammoS>0&&reloadS<=0){shootGun(bullets,origin,aim,g,true,std::string(g.name)=="Flare Gun"); ammoS--; fireCd=g.cooldown; fired=true;}} else if(slot==Slot::Melee){auto&m=MeleeWeapons[play.melee]; if(meleeCd<=0){meleeCd=m.cooldown; fired=true; for(auto&b:bots)if(b.alive&&distXZ(b.pos,player)<m.range){b.hp-=m.damage;spark(parts,b.pos,m.color,10);} if(isLan&&distXZ(remote.pos,player)<m.range){remote.hp-=m.damage;hitSeq++;}}} else if(slot==Slot::Utility && utilCd<=0){auto&u=UtilityWeapons[play.utility]; utilCd=u.cooldown; fired=true; std::string n=u.name; if(n=="Medkit")hp=std::min(100.0f,hp+50); else if(n=="Grappler"||n=="Warpstone")player=arenaClamp(Vector3Add(player,Vector3Scale(aim,n=="Warpstone"?12:9))); else if(n=="Jump Pad")vel.y=11; else if(n=="Elixir"){hp=std::min(100.0f,hp+35);stamina=100;} else {Bullet b{origin,Vector3Scale(aim,25),2.0f,55,true,1,.22f}; bullets.push_back(b);} }} if(fired)fireSeq++;
            Level lv=getLevel(selectedLevel); for(auto&m:mines){m.timer-=dt; for(auto&b:bots)if(b.alive&&distXZ(b.pos,m.pos)<m.radius){b.hp-=85;m.timer=0;spark(parts,b.pos,ORANGE,20);} } mines.erase(std::remove_if(mines.begin(),mines.end(),[](const Mine&m){return m.timer<=0;}),mines.end());
            if(!isLan){ for(auto&b:bots){ if(!b.alive){b.respawn-=dt; if(b.respawn<=0){b.alive=true;b.hp=b.maxhp;b.pos={(float)GetRandomValue(-320,320)/10,.9f,(float)GetRandomValue(-320,320)/10};} continue;} if(practice){ b.pos.x += sinf(GetTime()+b.pos.z)*dt*.8f; } if(b.freeze>0){b.freeze-=dt;continue;} if(b.stun>0)b.stun-=dt; if(!practice){Vector3 to=Vector3Subtract(player,b.pos);float d=Vector3Length(to);Vector3 dir=norm({to.x,0,to.z}); if(b.stun<=0&&d>4)b.pos=arenaClamp(Vector3Add(b.pos,Vector3Scale(dir,(2.0f+lv.skill*1.25f)*dt))); b.shot-=dt; if(b.shot<=0&&d<32&&b.stun<=0){Vector3 aimBot=norm(Vector3Subtract(Vector3Add(player,{0,1.1f,0}),Vector3Add(b.pos,{0,1.0f,0}))); float miss=std::max(.025f,(2.0f-lv.skill)*.07f); aimBot=norm({aimBot.x+(float)GetRandomValue(-100,100)/100.0f*miss,aimBot.y+(float)GetRandomValue(-100,100)/100.0f*miss,aimBot.z+(float)GetRandomValue(-100,100)/100.0f*miss}); bullets.push_back({Vector3Add(b.pos,{0,1,0}),Vector3Scale(aimBot,28+lv.skill*7),2.8f,(int)(8+lv.skill*6),false,0,.14f}); b.shot=clampf(1.15f-lv.skill*.22f,.30f,1.2f);}} }}
            int localHitDmg=0; for(auto&bu:bullets){ bu.pos=Vector3Add(bu.pos,Vector3Scale(bu.vel,dt)); bu.life-=dt; if(bu.kind==1)bu.vel.y-=8*dt; if(bu.player){ if(isLan && Vector3Distance(remote.pos,bu.pos)<(bu.kind?2.3f:.95f)){remote.hp-=bu.damage;localHitDmg+=bu.damage;bu.life=0;spark(parts,bu.pos,YELLOW,10);} else for(auto&b:bots)if(b.alive&&Vector3Distance(b.pos,bu.pos)<(bu.kind?2.3f:.95f)){b.hp-=bu.damage;bu.life=0;spark(parts,bu.pos,YELLOW,10);if(bu.kind)for(auto&bb:bots)if(bb.alive&&distXZ(bb.pos,bu.pos)<4)bb.hp-=bu.damage;} } else if(invuln<=0&&Vector3Distance(player,bu.pos)<1.1f){hp-=bu.damage;invuln=.42f;bu.life=0;spark(parts,player,RED,12);shake=.18f;} }
            if(localHitDmg>0)hitSeq++;
            bullets.erase(std::remove_if(bullets.begin(),bullets.end(),[](const Bullet&b){return b.life<=0||fabs(b.pos.x)>48||fabs(b.pos.z)>48||b.pos.y<-4;}),bullets.end()); for(auto&b:bots)if(b.alive&&b.hp<=0){b.alive=false;b.respawn=practice?1.0f:2.0f;kills++; if(!practice){save.kills++;save.coins+=10+selectedLevel;} if(GetRandomValue(0,100)<25)pickups.push_back({b.pos,GetRandomValue(0,1),true});} for(auto&p:pickups)if(p.active&&distXZ(p.pos,player)<1.4f){p.active=false;if(p.type==0)hp=std::min(100.0f,hp+25);else save.coins+=15;} for(auto&p:parts){p.pos=Vector3Add(p.pos,Vector3Scale(p.vel,dt));p.vel.y-=5*dt;p.life-=dt;} parts.erase(std::remove_if(parts.begin(),parts.end(),[](const Particle&p){return p.life<=0;}),parts.end());
            if(isLan){ if(localHitDmg>0) lan.sendState(makeState(player,yaw,pitch,hp,fireSeq,hitSeq,localHitDmg,play)); else lan.sendState(makeState(player,yaw,pitch,hp,fireSeq,hitSeq,0,play)); std::string msg; while(lan.recvMessage(msg)){ RemoteState before=remote; if(parseState(msg,remote)){ if(remote.hitSeq!=remoteSeenHit){ remoteSeenHit=remote.hitSeq; hp-=remote.hitDmg; invuln=.25f; spark(parts,player,RED,10); } if(remote.fireSeq!=remoteSeenFire){ remoteSeenFire=remote.fireSeq; Vector3 raim=forwardFrom(remote.yaw,remote.pitch); shootGun(bullets,Vector3Add(remote.pos,{0,1.2f,0}),raim,PrimaryWeapons[std::max(0,std::min(remote.p,(int)PrimaryWeapons.size()-1))],false,false); } } } if(remote.hp<=0){kills++;remote.hp=100;} }
            if(hp<=0){deaths++; if(!practice&&!isLan)save.deaths++; hp=100;player={isLan?(lan.hosting?-6.0f:6.0f):0,.9f,0};invuln=2.0f;} if(!practice&&!isLan&&kills>=lv.target){save.wins++;save.coins+=60+selectedLevel*20;if(save.highestLevel<selectedLevel+2&&save.highestLevel<levelCount())save.highestLevel=selectedLevel+2;saveGame(save);EnableCursor();screen=Screen::GameOver;} if(shake>0)shake-=dt; Vector3 head=Vector3Add(player,{0,1.35f,0}); if(firstPerson){cam.position=head;cam.target=Vector3Add(head,Vector3Scale(aim,10));}else{Vector3 desired=Vector3Add(head,Vector3Add(Vector3Scale(aim,-7.2f),{0,1.1f,0})); cam.position=Vector3Lerp(cam.position,desired,1-expf(-13*dt)); cam.target=Vector3Lerp(cam.target,Vector3Add(head,Vector3Scale(aim,4.0f)),1-expf(-18*dt));}
        }

        BeginDrawing();
        if(screen==Screen::Loading)drawLoading(loading);
        else if(screen==Screen::MainMenu){ title("Steel Arena Local", "v17: cleaner GUI, no shop/practice tip overlap"); std::vector<std::string> items={"Start Next Level","Choose Levels","Practice Mode","Shop / Buy Items","LAN Multiplayer","Credits","Quit"}; menuList(items,menuSel,70,170); drawTextCentered(TextFormat("Loadout: %s / %s / %s / %s",PrimaryWeapons[save.primary].name,SecondaryWeapons[save.secondary].name,MeleeWeapons[save.melee].name,UtilityWeapons[save.utility].name),GetScreenWidth()/2,592,20,LIGHTGRAY); drawTextCentered(TextFormat("Coins: %d   |   Unlocked Level: %d/50",save.coins,save.highestLevel),GetScreenWidth()/2,622,20,GOLD); }
        else if(screen==Screen::LanMenu){ title("LAN Multiplayer", "Two devices on the same Wi-Fi/LAN. Host generates code; joiner enters it."); std::vector<std::string> items={"Host LAN Match","Join LAN Match","Back"}; menuList(items,menuSel,70,190); DrawText("Note: allow the game through firewall if discovery does not connect.",70,410,22,SKYBLUE); }
        else if(screen==Screen::LanHost){ title("Host LAN Match", "Give this code to the other player on the same LAN."); DrawText(TextFormat("LAN CODE: %04d",lan.code),120,210,64,GOLD); DrawText(lan.status.c_str(),120,305,24,lan.connected?GREEN:LIGHTGRAY); DrawText("Enter = choose loadout/start once connected | Backspace = cancel",120,360,22,SKYBLUE); }
        else if(screen==Screen::LanJoin){ title("Join LAN Match", "Type the 4-digit code from the host, then press Enter."); DrawText(TextFormat("CODE: %04d",joinCode),120,210,64,GOLD); DrawText(lan.status.c_str(),120,305,24,lan.connected?GREEN:LIGHTGRAY); DrawText("Number keys = enter code | Backspace = erase | Esc = cancel",120,360,22,SKYBLUE); }
        else if(screen==Screen::LevelSelect){ titlePlain("Choose Level"); drawTextCentered("50 levels total. Locked levels unlock after clears.", GetScreenWidth()/2, 154, 18, Fade(RAYWHITE,.70f)); int panelW=900; int panelX=GetScreenWidth()/2-panelW/2; drawPanel(R((float)panelX,190,(float)panelW,430),SKYBLUE); int start=std::max(0,std::min(levelSel-5,levelCount()-10)); for(int row=0;row<10&&start+row<levelCount();row++){int i=start+row; Level l=getLevel(i); bool unlocked=i<save.highestLevel; Rectangle rr=R((float)panelX+28,218+row*38,(float)panelW-56,32); bool selected=i==levelSel; Color fill=selected?Fade(GOLD,.30f):(unlocked?Fade(WHITE,.08f):Fade(GRAY,.08f)); DrawRectangleRounded(rr,.12f,10,fill); DrawRectangleRoundedLines(rr,.12f,10,selected?GOLD:Fade(WHITE,.18f)); DrawText(TextFormat("%02d",i+1),(int)rr.x+18,(int)rr.y+7,20,unlocked?GOLD:GRAY); DrawText(l.name.c_str(),(int)rr.x+86,(int)rr.y+7,20,unlocked?RAYWHITE:GRAY); DrawText(unlocked?"UNLOCKED":"LOCKED",(int)(rr.x+rr.width-120),(int)rr.y+7,20,unlocked?GREEN:RED); } Level l=getLevel(levelSel); drawTextCentered(l.desc.c_str(),GetScreenWidth()/2,642,20,levelSel<save.highestLevel?RAYWHITE:GRAY); drawTextCentered("W/S choose | Enter opens loadout | Backspace menu",GetScreenWidth()/2,672,20,LIGHTGRAY); }
        else if(screen==Screen::Shop || screen==Screen::Loadout){
            bool isShop = screen == Screen::Shop;
            Tab tab = isShop ? shopTab : loadTab;
            int sel = isShop ? shopSel : loadSel;
            int scroll = isShop ? shopScroll : loadScroll;

            // No subtitle/tip here: it was colliding with tabs on smaller windows.
            titlePlain(isShop ? "Shop" : (practice ? "Practice Loadout" : "Choose Loadout"));

            const char* tabs[4] = {"PRIMARY", "SECONDARY", "MELEE", "UTILITY"};
            int sw = GetScreenWidth();
            int sh = GetScreenHeight();

            int tabGap = 12;
            int tabW = std::min(210, (sw - 120 - tabGap*3) / 4);
            int totalTabs = tabW*4 + tabGap*3;
            int tabX = sw/2 - totalTabs/2;
            for(int t=0;t<4;t++){
                drawPill(R((float)tabX + t*(tabW+tabGap), 160, (float)tabW, 42), tabs[t], (int)tab == t);
            }

            drawTextCentered(TextFormat("Coins: %d", save.coins), sw/2, 214, 22, GOLD);

            int count = itemCount(tab);
            int panelX = std::max(38, sw/2 - 540);
            int panelW = std::min(1080, sw - panelX*2);
            int top = 252;
            int bottomInfoH = 118;
            int footerY = sh - 42;
            int listH = std::max(250, sh - top - bottomInfoH - 82);
            int rowH = 42;
            int visible = std::max(4, std::min(9, listH / rowH));

            drawPanel(R((float)panelX, (float)top-12, (float)panelW, (float)(visible*rowH + 58)), SKYBLUE);

            int nameX = panelX + 54;
            int statX = panelX + panelW/2 - 80;
            int statusX = panelX + panelW - 185;
            int nameMax = std::max(150, statX - nameX - 20);
            int statMax = std::max(160, statusX - statX - 20);
            int statusMax = 160;

            DrawText("ITEM", nameX, top, 17, Fade(RAYWHITE,.62f));
            DrawText("STATS", statX, top, 17, Fade(RAYWHITE,.62f));
            DrawText("STATUS", statusX, top, 17, Fade(RAYWHITE,.62f));
            DrawLine(panelX+22, top+25, panelX+panelW-22, top+25, Fade(WHITE,.16f));

            int firstRowY = top + 34;
            for(int row=0; row<visible && row+scroll<count; row++){
                int i = row + scroll;
                bool isOwned = practice || lanMode;
                bool equipped = false;
                int price = 0;
                std::string name, detail;
                Color itemColor = WHITE;

                if(tab == Tab::Primary){
                    auto &w = PrimaryWeapons[i];
                    isOwned = isOwned || owned(save.primaryMask, i);
                    equipped = (isShop ? save.primary : play.primary) == i;
                    price = w.price; name = w.name; itemColor = w.color;
                    detail = TextFormat("DMG %d  MAG %d  CD %.2f", w.damage, w.mag, w.cooldown);
                }else if(tab == Tab::Secondary){
                    auto &w = SecondaryWeapons[i];
                    isOwned = isOwned || owned(save.secondaryMask, i);
                    equipped = (isShop ? save.secondary : play.secondary) == i;
                    price = w.price; name = w.name; itemColor = w.color;
                    detail = TextFormat("DMG %d  MAG %d  CD %.2f", w.damage, w.mag, w.cooldown);
                }else if(tab == Tab::Melee){
                    auto &w = MeleeWeapons[i];
                    isOwned = isOwned || owned(save.meleeMask, i);
                    equipped = (isShop ? save.melee : play.melee) == i;
                    price = w.price; name = w.name; itemColor = w.color;
                    detail = TextFormat("DMG %d  RNG %.1f  CD %.2f", w.damage, w.range, w.cooldown);
                }else{
                    auto &w = UtilityWeapons[i];
                    isOwned = isOwned || owned(save.utilityMask, i);
                    equipped = (isShop ? save.utility : play.utility) == i;
                    price = w.price; name = w.name; itemColor = w.color;
                    detail = TextFormat("Cooldown %.1f", w.cooldown);
                }

                Rectangle rowRect = R((float)panelX + 18, (float)firstRowY + row*rowH, (float)panelW - 36, (float)rowH - 7);
                bool selected = i == sel;
                Color fill = equipped ? Fade(GOLD,.22f) : (selected ? Fade(SKYBLUE,.26f) : Fade(WHITE,.065f));
                DrawRectangleRounded(rowRect,.16f,14,fill);
                DrawRectangleRoundedLines(rowRect,.16f,14,selected ? GOLD : Fade(WHITE,.13f));
                if(selected) DrawRectangleRounded(R(rowRect.x+4,rowRect.y+4,5,rowRect.height-8),.7f,8,GOLD);

                DrawCircle((int)rowRect.x+25, (int)rowRect.y+rowH/2-4, 9, itemColor);
                drawTextFit(name, nameX, (int)rowRect.y+9, nameMax, 18, RAYWHITE);
                drawTextFit(detail, statX, (int)rowRect.y+9, statMax, 17, LIGHTGRAY);
                std::string status = equipped ? "EQUIPPED" : (isShop ? (isOwned ? "EQUIP" : TextFormat("BUY %d", price)) : (isOwned ? "SELECT" : "LOCKED"));
                drawTextFit(status, statusX, (int)rowRect.y+9, statusMax, 17, equipped ? GOLD : (isOwned ? GREEN : RED));
            }

            if(count > visible){
                DrawText(TextFormat("%d / %d", sel+1, count), panelX+panelW-96, firstRowY + visible*rowH + 8, 17, LIGHTGRAY);
                DrawText("Use W/S to scroll", panelX+24, firstRowY + visible*rowH + 8, 17, Fade(RAYWHITE,.60f));
            }

            int infoY = firstRowY + visible*rowH + 44;
            Rectangle info = R((float)panelX, (float)infoY, (float)panelW, 84);
            drawPanel(info, GOLD);
            DrawText("CURRENT LOADOUT", panelX+24, infoY+16, 20, GOLD);
            int colY = infoY + 48;
            int colGap = panelW / 4;
            drawTextFit(TextFormat("P: %s", PrimaryWeapons[(isShop?save.primary:play.primary)].name), panelX+24, colY, colGap-32, 18, PrimaryWeapons[(isShop?save.primary:play.primary)].color);
            drawTextFit(TextFormat("S: %s", SecondaryWeapons[(isShop?save.secondary:play.secondary)].name), panelX+24+colGap, colY, colGap-32, 18, SecondaryWeapons[(isShop?save.secondary:play.secondary)].color);
            drawTextFit(TextFormat("M: %s", MeleeWeapons[(isShop?save.melee:play.melee)].name), panelX+24+colGap*2, colY, colGap-32, 18, MeleeWeapons[(isShop?save.melee:play.melee)].color);
            drawTextFit(TextFormat("U: %s", UtilityWeapons[(isShop?save.utility:play.utility)].name), panelX+24+colGap*3, colY, colGap-32, 18, UtilityWeapons[(isShop?save.utility:play.utility)].color);

            if(!isShop){
                const char* modeText = lanMode ? "LAN MATCH" : (practice ? "PRACTICE RANGE" : TextFormat("LEVEL %d", selectedLevel+1));
                drawTextCentered(modeText, sw/2, infoY+16, 20, SKYBLUE);
                DrawText("Press F to start", panelX + panelW - 180, infoY+16, 20, GOLD);
            }

            DrawRectangle(0, sh-54, sw, 54, Fade(BLACK,.38f));
            drawTextCentered(isShop ? "W/S choose  •  A/D change section  •  Enter buy/equip  •  Backspace menu"
                                    : "W/S choose  •  A/D change section  •  Enter select  •  F start  •  Backspace menu",
                             sw/2, footerY, 19, Fade(RAYWHITE,.86f));
        }
        else if(screen==Screen::Credits){ ClearBackground({8,10,18,255}); DrawText("Credits",80,90,54,GOLD); DrawText("Made by y8tireu",90,180,34,RAYWHITE); DrawText("LAN support uses local UDP packets only. No internet/server/database.",90,235,22,LIGHTGRAY); DrawText("Backspace or Enter to return",90,300,22,SKYBLUE); }
        else { bool isLan=screen==Screen::LanPlaying; Camera3D dc=cam; if(shake>0){dc.target.x+=GetRandomValue(-10,10)*.01f*shake;dc.target.z+=GetRandomValue(-10,10)*.01f*shake;} ClearBackground({6,8,14,255}); BeginMode3D(dc); drawArena(selectedLevel,mines,practice||isLan); drawPlayerModel(player,BLUE,firstPerson); if(isLan&&remote.valid) drawPlayerModel(remote.pos,RED,false); for(auto&b:bots)drawBot(b,selectedLevel,practice); for(auto&bu:bullets)DrawSphere(bu.pos,bu.radius,bu.player?YELLOW:RED); for(auto&p:parts)DrawSphere(p.pos,.08f,Fade(p.color,p.life*2)); for(auto&p:pickups)if(p.active)DrawSphere(p.pos,.35f,p.type==0?GREEN:GOLD); EndMode3D(); drawHud(play,slot,ammoP,ammoS,hp,stamina,kills,deaths,selectedLevel,firstPerson,practice,isLan,remote.hp); DrawCircle(GetScreenWidth()/2,GetScreenHeight()/2,4,WHITE); DrawLine(GetScreenWidth()/2-15,GetScreenHeight()/2,GetScreenWidth()/2+15,GetScreenHeight()/2,Fade(WHITE,.5f)); DrawLine(GetScreenWidth()/2,GetScreenHeight()/2-15,GetScreenWidth()/2,GetScreenHeight()/2+15,Fade(WHITE,.5f)); if(isLan && GetTime()-remote.lastSeen>2.5) DrawText("Waiting for LAN packets... check firewall/same Wi-Fi",GetScreenWidth()/2-280,80,22,ORANGE); if(screen==Screen::Paused){DrawRectangle(0,0,GetScreenWidth(),GetScreenHeight(),Fade(BLACK,.65f));DrawText("PAUSED",GetScreenWidth()/2-100,GetScreenHeight()/2-70,50,GOLD);DrawText("P/Enter Resume | R Restart | Backspace Main Menu",GetScreenWidth()/2-300,GetScreenHeight()/2,24,RAYWHITE);} if(screen==Screen::GameOver){DrawRectangle(0,0,GetScreenWidth(),GetScreenHeight(),Fade(BLACK,.72f));DrawText("LEVEL CLEAR",GetScreenWidth()/2-170,GetScreenHeight()/2-110,50,GOLD);DrawText(TextFormat("Kills: %d   Deaths: %d   Coins: %d",kills,deaths,save.coins),GetScreenWidth()/2-230,GetScreenHeight()/2-30,24,RAYWHITE);DrawText("Enter next level loadout | Backspace main menu",GetScreenWidth()/2-300,GetScreenHeight()/2+35,24,SKYBLUE);} }
        EndDrawing();
    }
    saveGame(save); lan.close(); CloseAudioDevice(); CloseWindow(); return 0;
}
