// weapon.cpp: all shooting and effects code, projectile management
#include "game.h"
#include "engine.h"

namespace game
{
    static const int MONSTERDAMAGEFACTOR = 4;
    static const int OFFSETMILLIS = 500;
    vec sg[SGRAYS];

    struct hitmsg
    {
        int target, lifesequence, info1, info2;
        ivec dir;
    };
    vector<hitmsg> hits;

    VARP(maxdebris, 10, 25, 1000);
    VARP(maxbarreldebris, 5, 10, 1000);

    ICOMMAND(getweapon, "", (), intret(player1->gunselect));

    void gunselect(int gun, fpsent *d)
    {
        if(gun!=d->gunselect)
        {
            addmsg(N_GUNSELECT, "rci", d, gun);
            playsound(S_WEAPLOAD, &d->o);
        }
        d->gunselect = gun;
    }

    void nextweapon(int dir, bool force = false)
    {
        if(player1->state!=CS_ALIVE) return;
        dir = (dir < 0 ? NUMGUNS-1 : 1);
        int gun = player1->gunselect;
        loopi(NUMGUNS)
        {
            gun = (gun + dir)%NUMGUNS;
            if(force || player1->ammo[gun]) break;
        }
        if(gun != player1->gunselect) gunselect(gun, player1);
        else playsound(S_NOAMMO);
    }
    ICOMMAND(nextweapon, "ii", (int *dir, int *force), nextweapon(*dir, *force!=0));

    int getweapon(const char *name)
    {
        const char *abbrevs[] = { "FI", "SG", "CG", "RL", "RI", "GL", "PI", "BO" };
        if(isdigit(name[0])) return parseint(name);
        else loopi(sizeof(abbrevs)/sizeof(abbrevs[0])) if(!strcasecmp(abbrevs[i], name)) return i;
        return -1;
    }

    void setweapon(const char *name, bool force = false)
    {
        int gun = getweapon(name);
        if(player1->state!=CS_ALIVE || gun<GUN_FIST || gun>GUN_BOMB) return;
        if(force || player1->ammo[gun]) gunselect(gun, player1);
        else playsound(S_NOAMMO);
    }
    ICOMMAND(setweapon, "si", (char *name, int *force), setweapon(name, *force!=0));

    void cycleweapon(int numguns, int *guns, bool force = false)
    {
        if(numguns<=0 || player1->state!=CS_ALIVE) return;
        int offset = 0;
        loopi(numguns) if(guns[i] == player1->gunselect) { offset = i+1; break; }
        loopi(numguns)
        {
            int gun = guns[(i+offset)%numguns];
            if(gun>=0 && gun<NUMGUNS && (force || player1->ammo[gun]))
            {
                gunselect(gun, player1);
                return;
            }
        }
        playsound(S_NOAMMO);
    }
    ICOMMAND(cycleweapon, "V", (tagval *args, int numargs),
    {
         int numguns = min(numargs, 8);
         int guns[8];
         loopi(numguns) guns[i] = getweapon(args[i].getstr());
         cycleweapon(numguns, guns);
    });

    void weaponswitch(fpsent *d)
    {
        if(d->state!=CS_ALIVE) return;
        int s = d->gunselect;
        if     (s!=GUN_CG     && d->ammo[GUN_CG])     s = GUN_CG;
        else if(s!=GUN_RL     && d->ammo[GUN_RL])     s = GUN_RL;
        else if(s!=GUN_SG     && d->ammo[GUN_SG])     s = GUN_SG;
        else if(s!=GUN_RIFLE  && d->ammo[GUN_RIFLE])  s = GUN_RIFLE;
        else if(s!=GUN_GL     && d->ammo[GUN_GL])     s = GUN_GL;
        else if(s!=GUN_PISTOL && d->ammo[GUN_PISTOL]) s = GUN_PISTOL;
        else if(s!=GUN_BOMB   && d->ammo[GUN_BOMB])   s = GUN_BOMB;
        else s = d->backupweapon; // switch to the fallback weapon
        gunselect(s, d);
    }

    ICOMMAND(weapon, "V", (tagval *args, int numargs),
    {
        if(player1->state!=CS_ALIVE) return;
        loopi(8)
        {
            const char *name = i < numargs ? args[i].getstr() : "";
            if(name[0])
            {
                int gun = getweapon(name);
                if(gun >= GUN_FIST && gun <= GUN_BOMB && gun != player1->gunselect && player1->ammo[gun]) { gunselect(gun, player1); return; }
            } else { weaponswitch(player1); return; }
        }
        playsound(S_NOAMMO);
    });

    void offsetray(const vec &from, const vec &to, int spread, float range, vec &dest)
    {
        float f = to.dist(from)*spread/1000;
        for(;;)
        {
            #define RNDD rnd(101)-50
            vec v(RNDD, RNDD, RNDD);
            if(v.magnitude()>50) continue;
            v.mul(f);
            v.z /= 2;
            dest = to;
            dest.add(v);
            vec dir = dest;
            dir.sub(from);
            dir.normalize();
            raycubepos(from, dir, dest, range, RAY_CLIPMAT|RAY_ALPHAPOLY);
            return;
        }
    }

    void createrays(const vec &from, const vec &to)             // create random spread of rays for the shotgun
    {
        loopi(SGRAYS) offsetray(from, to, SGSPREAD, guns[GUN_SG].range, sg[i]);
    }

    vector<bouncer *> bouncers;

    vec hudgunorigin(int gun, const vec &from, const vec &to, fpsent *d);

    void newbouncer(const vec &from, const vec &to, bool local, int id, fpsent *owner, int type, int lifetime, int speed, entitylight *light = NULL, int generation = 0)
    {
        bouncer &bnc = *bouncers.add(new bouncer);
        bnc.o = from;
        switch(type)
        {
            case BNC_DEBRIS:
                bnc.radius = bnc.xradius = bnc.yradius = bnc.eyeheight = bnc.aboveeye = 0.5f;
                break;
            case BNC_BOMB:
                bnc.radius = bnc.xradius = bnc.yradius = 2.5f;
                bnc.eyeheight = bnc.aboveeye = 1.5f;
                break;
            case BNC_SPLINTER:
                bnc.radius = bnc.xradius = bnc.yradius = bnc.eyeheight = bnc.aboveeye = 1.5f; // TODO: adjust
                break;
            default:
                bnc.radius = bnc.xradius = bnc.yradius = bnc.eyeheight = bnc.aboveeye = 1.5f;
                break;
        }
        bnc.lifetime = lifetime;
        bnc.local = local;
        bnc.owner = owner;
        bnc.bouncetype = type;
        bnc.generation = generation;
        bnc.id = local ? lastmillis : id;
        if(light) bnc.light = *light;

        switch(type)
        {
            case BNC_DEBRIS: case BNC_BARRELDEBRIS: bnc.variant = rnd(4); break;
            case BNC_GIBS: bnc.variant = rnd(3); break;
        }

        vec dir(to);
        dir.sub(from).normalize();
        bnc.vel = dir;
        bnc.vel.mul(speed);

        switch(type)
        {
            case BNC_GRENADE:
                // bnc.o.add(dir).normalize();
                avoidcollision(&bnc, dir, owner, 0.1f);
                bnc.offset = hudgunorigin(GUN_GL, from, to, owner);
                if(owner==hudplayer() && !isthirdperson()) bnc.offset.sub(owner->o).rescale(16).add(owner->o);
                break;
            case BNC_BOMB:
                avoidcollision(&bnc, dir, owner, 4.5f); // we need much more radius for bombs
                bnc.offset = hudgunorigin(GUN_BOMB, from, to, owner);
                if(owner==hudplayer() && !isthirdperson()) bnc.offset.sub(owner->o).rescale(2).add(owner->o);
                break;
            default:
                avoidcollision(&bnc, dir, owner, 0.1f);
                bnc.offset = from;
                break;
        }
        bnc.offset.sub(bnc.o);
        bnc.offsetmillis = OFFSETMILLIS;

        bnc.resetinterp();
    }

    void bounced(physent *d, const vec &surface)
    {
        if(d->type != ENT_BOUNCE) return;
        bouncer *b = (bouncer *)d;
        if(b->bouncetype != BNC_GIBS || b->bounces >= 2) return;
        b->bounces++;
        adddecal(DECAL_BLOOD, vec(b->o).sub(vec(surface).mul(b->radius)), surface, 2.96f/b->bounces, bvec(0x60, 0xFF, 0xFF), rnd(4));
    }

    void spawnnextsplinter(const vec &p, const vec &vel, fpsent *d, int generation)
    {
        generation--;
        if(generation<1) return;
        vec to(vel);
        float fac = ((d->bombradius*d->bombradius)+30.0f)*(d->bombradius-generation+1);
        to.x = (to.x * fac);// + (rnd(60)-15.0f);
        to.y = (to.y * fac) + (((float)rnd(120))-60.0f);
        to.z+=5.0f;
        to.add(p);
        newbouncer(p, to, true, 0, d, BNC_SPLINTER, 210, 140, NULL, generation); // lifetime, speed
    }

    void spawnsplinters(const vec &p, fpsent *d)
    {
        for(int i=1; i<=36; i++) // je fortgeschrittener, desto weniger verzweigungen
        {
            vec to(sin(36.0f/((float) i)), cos(36.0f/((float) i)), 5);
            float fac = (d->bombradius*d->bombradius)+30.0f;
            to.x*=fac;
            to.y*=fac;
            to.add(p);
            newbouncer(p, to, true, 0, d, BNC_SPLINTER, 210, 140, NULL, d->bombradius); // 1+((d->bombradius-1)*2)); // 1, 3, 5, 7, ... generations
        }
    }

    VARP(bombsmoke, 0, 1, 1);

    void updatebouncers(int time)
    {
        loopv(bouncers)
        {
            bouncer &bnc = *bouncers[i];
            if(bnc.bouncetype==BNC_GRENADE && bnc.vel.magnitude() > 50.0f)
            {
                vec pos(bnc.o);
                pos.add(vec(bnc.offset).mul(bnc.offsetmillis/float(OFFSETMILLIS)));
                regular_particle_splash(PART_SMOKE, 1, 150, pos, 0x404040, 2.4f, 50, -20);
            }
            else if(bnc.bouncetype==BNC_BOMB)
            {
                vec pos(bnc.o);
                pos.add(vec(bnc.offset).mul(bnc.offsetmillis/float(OFFSETMILLIS)));
                if(bombsmoke) regular_particle_splash(PART_SMOKE, 1, 150, pos, 0x0080f0, 6.4f, 120, -120);
            }
            vec old(bnc.o);
            bool stopped = false;
            if(bnc.bouncetype==BNC_GRENADE) stopped = bounce(&bnc, 0.6f, 0.5f) || (bnc.lifetime -= time)<0;
	          else if(bnc.bouncetype==BNC_BOMB)
            {
	              bounce(&bnc, 0.2f, 0.3f);
                stopped = (bnc.lifetime -= time)<0;
            }
            else
            {
                // cheaper variable rate physics for debris, gibs, etc.
                for(int rtime = time; rtime > 0;)
                {
                    int qtime = min(30, rtime);
                    rtime -= qtime;
                    if((bnc.lifetime -= qtime)<0 || bounce(&bnc, qtime/1000.0f, 0.6f, 0.5f)) { stopped = true; break; }
                }
            }
            if(stopped)
            {
                if(bnc.bouncetype==BNC_GRENADE)
                {
                    int qdam = guns[GUN_GL].damage*(bnc.owner->quadmillis ? 4 : 1);
                    hits.setsize(0);
                    explode(bnc.local, bnc.owner, bnc.o, NULL, qdam, GUN_GL);
                    adddecal(DECAL_SCORCH, bnc.o, vec(0, 0, 1), RL_DAMRAD/2);
                    if(bnc.local)
                        addmsg(N_EXPLODE, "rci3iv", bnc.owner, lastmillis-maptime, GUN_GL, bnc.id-maptime,
                                hits.length(), hits.length()*sizeof(hitmsg)/sizeof(int), hits.getbuf());
                }
                else if(bnc.bouncetype==BNC_BOMB)
                {
                    hits.setsize(0);
                    explode(bnc.local, bnc.owner, bnc.o, NULL, guns[GUN_BOMB].damage, GUN_BOMB);
                    adddecal(DECAL_SCORCH, bnc.o, vec(0, 0, 1), bnc.owner->bombradius*RL_DAMRAD/2);
                    spawnsplinters(bnc.o, bnc.owner); // starts with 3+x generations
                    if(bnc.local)
                        addmsg(N_EXPLODE, "rci3iv", bnc.owner, lastmillis-maptime, GUN_BOMB, bnc.id-maptime,
                                hits.length(), hits.length()*sizeof(hitmsg)/sizeof(int), hits.getbuf());
                }
                else if(bnc.bouncetype==BNC_SPLINTER)
                {
                    hits.setsize(0);
                    explode(bnc.local, bnc.owner, bnc.o, NULL, guns[GUN_SPLINTER].damage, GUN_SPLINTER);
                    spawnnextsplinter(bnc.o, bnc.vel, bnc.owner, bnc.generation);
                    if(bnc.local)
                        addmsg(N_EXPLODE, "rci3iv", bnc.owner, lastmillis-maptime, GUN_SPLINTER, bnc.id-maptime,
                                hits.length(), hits.length()*sizeof(hitmsg)/sizeof(int), hits.getbuf());
                }
                delete bouncers.remove(i--);
            }
            else
            {
                bnc.roll += old.sub(bnc.o).magnitude()/(4*RAD);
                bnc.offsetmillis = max(bnc.offsetmillis-time, 0);
            }
        }
    }

    void removebouncers(fpsent *owner)
    {
        loopv(bouncers) if(bouncers[i]->owner==owner) { delete bouncers[i]; bouncers.remove(i--); }
    }

    void clearbouncers() { bouncers.deletecontents(); }

    vector<projectile> projs;

    void clearprojectiles() { projs.shrink(0); }

    void newprojectile(const vec &from, const vec &to, float speed, bool local, int id, fpsent *owner, int gun)
    {
        projectile &p = projs.add();
        p.dir = vec(to).sub(from).normalize();
        p.o = from;
        p.to = to;
        p.offset = hudgunorigin(gun, from, to, owner);
        p.offset.sub(from);
        p.speed = speed;
        p.local = local;
        p.owner = owner;
        p.gun = gun;
        p.offsetmillis = OFFSETMILLIS;
        p.id = local ? lastmillis : id;
    }

    void removeprojectiles(fpsent *owner)
    {
        // can't use loopv here due to strange GCC optimizer bug
        int len = projs.length();
        loopi(len) if(projs[i].owner==owner) { projs.remove(i--); len--; }
    }

    VARP(blood, 0, 1, 1);

    void damageeffect(int damage, fpsent *d, bool thirdperson)
    {
        vec p = d->o;
        p.z += 0.6f*(d->eyeheight + d->aboveeye) - d->eyeheight;
        if(blood) particle_splash(PART_BLOOD, damage/10, 1000, p, 0x60FFFF, 2.96f);
        if(thirdperson)
        {
            defformatstring(ds)("%d", damage);
            particle_textcopy(d->abovehead(), ds, PART_TEXT, 2000, 0xFF4B19, 4.0f, -8);
        }
    }

    void spawnbouncer(const vec &p, const vec &vel, fpsent *d, int type, entitylight *light = NULL)
    {
        vec to(rnd(100)-50, rnd(100)-50, rnd(100)-50);
        if(to.iszero()) to.z += 1;
        to.normalize();
        to.add(p);
        newbouncer(p, to, true, 0, d, type, rnd(1000)+1000, rnd(100)+20, light);
    }

    void gibeffect(int damage, const vec &vel, fpsent *d)
    {
        if(!blood || damage <= 0) return;
        vec from = d->abovehead();
        loopi(min(damage/25, 40)+1) spawnbouncer(from, vel, d, BNC_GIBS);
    }

    void hit(int damage, dynent *d, fpsent *at, const vec &vel, int gun, float info1, int info2 = 1)
    {
        // conoutf("hit from %s (cn=%i); damage=%i gun=%i target-type=%i", at->name, at->clientnum, damage, gun, d->type);
        if(at==player1 && d!=at)
        {
            extern int hitsound;
            if(hitsound && lasthit != lastmillis) playsound(S_HIT);
            lasthit = lastmillis;
        }

        if(d->type==ENT_INANIMATE)
        {
            // conoutf("hit a movable");
            hitmovable(damage, (movable *)d, at, vel, gun);
            return;
        }

        fpsent *f = (fpsent *)d;

        f->lastpain = lastmillis;
        if(at->type==ENT_PLAYER && !isteam(at->team, f->team)) at->totaldamage += damage;

        if(f->type==ENT_AI || !m_mp(gamemode) || f==at) f->hitpush(damage, vel, at, gun);

        if(f->type==ENT_AI) hitmonster(damage, (monster *)f, at, vel, gun);
        else if(!m_mp(gamemode)) damaged(damage, f, at);
        else
        {
            hitmsg &h = hits.add();
            h.target = f->clientnum;
            h.lifesequence = f->lifesequence;
            h.info1 = int(info1*DMF);
            h.info2 = info2;
            h.dir = f==at ? ivec(0, 0, 0) : ivec(int(vel.x*DNF), int(vel.y*DNF), int(vel.z*DNF));
            if(at==player1)
            {
                damageeffect(damage, f);
                if(f==player1)
                {
                    damageblend(damage);
                    damagecompass(damage, at ? at->o : f->o);
                    playsound(S_PAIN6);
                }
                else playsound(S_PAIN1+rnd(5), &f->o);
            }
        }
    }

    void hitpush(int damage, dynent *d, fpsent *at, vec &from, vec &to, int gun, int rays)
    {
        hit(damage, d, at, vec(to).sub(from).normalize(), gun, from.dist(to), rays);
    }

    float projdist(dynent *o, vec &dir, const vec &v)
    {
        vec middle = o->o;
        middle.z += (o->aboveeye-o->eyeheight)/2;
        float dist = middle.dist(v, dir);
        dir.div(dist);
        if(dist<0) dist = 0;
        return dist;
    }

    void radialeffect(dynent *o, const vec &v, int qdam, fpsent *at, int gun)
    {
        if(o->state!=CS_ALIVE) return;
        vec dir;
        float dist = projdist(o, dir, v);
        switch(gun)
        {
            case GUN_BOMB:
                if(dist<RL_DAMRAD)
                    hit(guns[gun].damage, o, at, dir, gun, dist); // damage does not depend on the distance
                break;
            case GUN_SPLINTER:
                if(dist<RL_DAMRAD)
                    hit(guns[gun].damage, o, at, dir, gun, dist); // damage does not depend on the distance
                break;
            default:
                if(dist<RL_DAMRAD)
                {
                    int damage = (int)(qdam*(1-dist/RL_DISTSCALE/RL_DAMRAD));
                    if(gun==GUN_RL && o==at) damage /= RL_SELFDAMDIV;
                    hit(damage, o, at, dir, gun, dist);
                }
                break;
        }
    }

    void radialbombeffect(bouncer *b, const vec &v)
    {
        if(b->bouncetype!=BNC_BOMB) return;
        vec dir;
        float dist = b->o.dist(v, dir);
        dir.div(dist);
        if(dist<0) dist = 0;
        if(dist < RL_DAMRAD) b->lifetime = 100; // TODO: maybe RL_DAMRAD is too big!
    }

    void explode(bool local, fpsent *owner, const vec &v, dynent *safe, int damage, int gun)
    {
        int rfactor = 1,
            maxsize = RL_DAMRAD * rfactor,
            size = 4.0f * rfactor,
            fade = gun!=GUN_BOMB && gun!=GUN_SPLINTER ? -1 : int((maxsize-size)*7), // explosion speed, lower=faster
            numdebris = gun==GUN_BARREL ? rnd(max(maxbarreldebris-5, 1))+5 : rnd(maxdebris-5)+5;
        vec debrisvel = owner->o==v ? vec(0, 0, 0) : vec(owner->o).sub(v).normalize(), debrisorigin(v);
        if(gun!=GUN_SPLINTER)
        {
            particle_splash(PART_SPARK, 200, 300, v, 0xB49B4B, 0.24f*rfactor);
            playsound(S_RLHIT, &v);
            particle_fireball(v, maxsize, gun!=GUN_GL ? PART_EXPLOSION : PART_EXPLOSION_BLUE, fade, gun!=GUN_GL ? (gun==GUN_BOMB ? 0x004D30 : 0xFF8080) : 0x80FFFF, size);
        }
        else
            particle_fireball(v, maxsize, gun!=GUN_GL ? PART_EXPLOSION : PART_EXPLOSION_BLUE, fade, 0x004D30, size);
        switch(gun)
        {
            case GUN_RL:
                adddynlight(v, 1.15f*RL_DAMRAD, vec(2, 1.5f, 1), 900, 100, 0, RL_DAMRAD/2, vec(1, 0.75f, 0.5f));
                debrisorigin.add(vec(debrisvel).mul(8));
                break;
            case GUN_GL:
                adddynlight(v, 1.15f*RL_DAMRAD, vec(0.5f, 1.5f, 2), 900, 100, 0, 8, vec(0.25f, 1, 1));
                break;
            case GUN_BOMB:
                // TODO: COMMENT IN: adddynlight(v, owner->bombradius*RL_DAMRAD, vec(0.5f, 1.5f, 2), 900, 100, 0, 8, vec(1, 1, 0.25f));
                if(owner->ammo[GUN_BOMB] < itemstats[P_AMMO_BO].max) owner->ammo[GUN_BOMB]++; // add a bomb if the bomb explodes
                break;
            case GUN_SPLINTER:
                // no dynlight
                break;
            default:
                adddynlight(v, 1.15f*RL_DAMRAD, vec(2, 1.5f, 1), 900, 100);
                break;
        }
        if(numdebris && gun!=GUN_SPLINTER)
        {
            entitylight light;
            lightreaching(debrisorigin, light.color, light.dir);
            loopi(numdebris)
                spawnbouncer(debrisorigin, debrisvel, owner, gun==GUN_BARREL ? BNC_BARRELDEBRIS : BNC_DEBRIS, &light);
        }
        if(!local && !m_obstacles) return;
        loopi(numdynents())
        {
            dynent *o = iterdynents(i);
            if(o==safe) continue;
            radialeffect(o, v, damage, owner, gun);
        }
        if(m_bomb && (gun==GUN_BOMB || gun==GUN_SPLINTER)) // in bomb mode projectiles hits other projectiles
        {
            int len = bouncers.length();
            loopi(len) radialbombeffect(bouncers[i], v);
        }

    }

    void projsplash(projectile &p, vec &v, dynent *safe, int damage)
    {
        if(guns[p.gun].part)
        {
            particle_splash(PART_SPARK, 100, 200, v, 0xB49B4B, 0.24f);
            playsound(S_FEXPLODE, &v);
            // no push?
        }
        else
        {
            explode(p.local, p.owner, v, safe, damage, GUN_RL);
            adddecal(DECAL_SCORCH, v, vec(p.dir).neg(), RL_DAMRAD/2);
        }
    }

    void explodeeffects(int gun, fpsent *d, bool local, int id)
    {
        if(local) return;
        switch(gun)
        {
            case GUN_RL:
                loopv(projs)
                {
                    projectile &p = projs[i];
                    if(p.gun == gun && p.owner == d && p.id == id && !p.local)
                    {
                        vec pos(p.o);
                        pos.add(vec(p.offset).mul(p.offsetmillis/float(OFFSETMILLIS)));
                        explode(p.local, p.owner, pos, NULL, 0, GUN_RL);
                        adddecal(DECAL_SCORCH, pos, vec(p.dir).neg(), RL_DAMRAD/2);
                        projs.remove(i);
                        break;
                    }
                }
                break;
            case GUN_GL:
                loopv(bouncers)
                {
                    bouncer &b = *bouncers[i];
                    if(b.bouncetype == BNC_GRENADE && b.owner == d && b.id == id && !b.local)
                    {
                        vec pos(b.o);
                        pos.add(vec(b.offset).mul(b.offsetmillis/float(OFFSETMILLIS)));
                        explode(b.local, b.owner, pos, NULL, 0, GUN_GL);
                        adddecal(DECAL_SCORCH, pos, vec(0, 0, 1), RL_DAMRAD/2);
                        delete bouncers.remove(i);
                        break;
                    }
                }
                break;
            case GUN_BOMB:
                loopv(bouncers)
                {
                    bouncer &b = *bouncers[i];
                    if(b.bouncetype == BNC_BOMB && b.owner == d && b.id == id && !b.local)
                    {
                        vec pos(b.o);
                        pos.add(vec(b.offset).mul(b.offsetmillis/float(OFFSETMILLIS)));
                        explode(b.local, b.owner, pos, NULL, 0, GUN_BOMB);
                        // adddecal(DECAL_SCORCH, pos, vec(0, 0, 1), b.owner->bombradius*RL_DAMRAD/2);
                        spawnsplinters(b.o, b.owner);
                        delete bouncers.remove(i);
                        break;
                    }
                }
                break;
            case GUN_SPLINTER:
                loopv(bouncers)
                {
                    bouncer &b = *bouncers[i];
                    if(b.bouncetype == BNC_SPLINTER && b.owner == d && b.id == id && !b.local)
                    {
                        vec pos(b.o);
                        pos.add(vec(b.offset).mul(b.offsetmillis/float(OFFSETMILLIS)));
                        explode(b.local, b.owner, pos, NULL, 0, GUN_SPLINTER);
                        spawnnextsplinter(b.o, b.vel, b.owner, b.generation);
                        delete bouncers.remove(i);
                        break;
                    }
                }
                break;
            default:
                break;
        }
    }

    bool projdamage(dynent *o, projectile &p, vec &v, int qdam)
    {
        if(o->state!=CS_ALIVE) return false;
        if(!intersect(o, p.o, v)) return false;
        projsplash(p, v, o, qdam);
        vec dir;
        projdist(o, dir, v);
        hit(qdam, o, p.owner, dir, p.gun, 0);
        return true;
    }

    void updateprojectiles(int time)
    {
        loopv(projs)
        {
            projectile &p = projs[i];
            p.offsetmillis = max(p.offsetmillis-time, 0);
            int qdam = guns[p.gun].damage*(p.owner->quadmillis ? 4 : 1);
            if(p.owner->type==ENT_AI) qdam /= MONSTERDAMAGEFACTOR;
            vec v;
            float dist = p.to.dist(p.o, v);
            float dtime = dist*1000/p.speed;
            if(time > dtime) dtime = time;
            v.mul(time/dtime);
            v.add(p.o);
            bool exploded = false;
            hits.setsize(0);
            if(p.local)
            {
                loopj(numdynents())
                {
                    dynent *o = iterdynents(j);
                    if(p.owner==o || o->o.reject(v, 10.0f)) continue;
                    if(projdamage(o, p, v, qdam)) { exploded = true; break; }
                }
            }
            if(!exploded)
            {
                if(dist<4)
                {
                    if(p.o!=p.to) // if original target was moving, reevaluate endpoint
                    {
                        if(raycubepos(p.o, p.dir, p.to, 0, RAY_CLIPMAT|RAY_ALPHAPOLY)>=4) continue;
                    }
                    projsplash(p, v, NULL, qdam);
                    exploded = true;
                }
                else
                {
                    vec pos(v);
                    pos.add(vec(p.offset).mul(p.offsetmillis/float(OFFSETMILLIS)));
                    if(guns[p.gun].part)
                    {
                         regular_particle_splash(PART_SMOKE, 2, 300, pos, 0x404040, 0.6f, 150, -20);
                         int color = 0xFFFFFF;
                         switch(guns[p.gun].part)
                         {
                            case PART_FIREBALL1: color = 0xFFC8C8; break;
                         }
                         particle_splash(guns[p.gun].part, 1, 1, pos, color, 4.8f, 150, 20);
                    }
                    else regular_particle_splash(PART_SMOKE, 2, 300, pos, 0x404040, 2.4f, 50, -20);
                }
            }
            if(exploded)
            {
                if(p.local)
                    addmsg(N_EXPLODE, "rci3iv", p.owner, lastmillis-maptime, p.gun, p.id-maptime,
                            hits.length(), hits.length()*sizeof(hitmsg)/sizeof(int), hits.getbuf());
                projs.remove(i--);
            }
            else p.o = v;
        }
    }

    extern int chainsawhudgun;

    VARP(muzzleflash, 0, 1, 1);
    VARP(muzzlelight, 0, 1, 1);

    void shoteffects(int gun, const vec &from, const vec &to, fpsent *d, bool local, int id, int prevaction)     // create visual effect from a shot
    {
        int sound = guns[gun].sound, pspeed = 25;
        switch(gun)
        {
            case GUN_FIST:
                if(d->type==ENT_PLAYER && chainsawhudgun) sound = S_CHAINSAW_ATTACK;
                break;

            case GUN_SG:
            {
                if(!local) createrays(from, to);
                if(muzzleflash && d->muzzle.x >= 0)
                    particle_flare(d->muzzle, d->muzzle, 200, PART_MUZZLE_FLASH3, 0xFFFFFF, 2.75f, d);
                loopi(SGRAYS)
                {
                    particle_splash(PART_SPARK, 20, 250, sg[i], 0xB49B4B, 0.24f);
                    particle_flare(hudgunorigin(gun, from, sg[i], d), sg[i], 300, PART_STREAK, 0xFFC864, 0.28f);
                    if(!local) adddecal(DECAL_BULLET, sg[i], vec(from).sub(sg[i]).normalize(), 2.0f);
                }
                if(muzzlelight) adddynlight(hudgunorigin(gun, d->o, to, d), 30, vec(0.5f, 0.375f, 0.25f), 100, 100, DL_FLASH, 0, vec(0, 0, 0), d);
                break;
            }

            case GUN_CG:
            case GUN_PISTOL:
            {
                particle_splash(PART_SPARK, 200, 250, to, 0xB49B4B, 0.24f);
                particle_flare(hudgunorigin(gun, from, to, d), to, 600, PART_STREAK, 0xFFC864, 0.28f);
                if(muzzleflash && d->muzzle.x >= 0)
                    particle_flare(d->muzzle, d->muzzle, gun==GUN_CG ? 100 : 200, PART_MUZZLE_FLASH1, 0xFFFFFF, gun==GUN_CG ? 2.25f : 1.25f, d);
                if(!local) adddecal(DECAL_BULLET, to, vec(from).sub(to).normalize(), 2.0f);
                if(muzzlelight) adddynlight(hudgunorigin(gun, d->o, to, d), gun==GUN_CG ? 30 : 15, vec(0.5f, 0.375f, 0.25f), gun==GUN_CG ? 50 : 100, gun==GUN_CG ? 50 : 100, DL_FLASH, 0, vec(0, 0, 0), d);
                break;
            }

            case GUN_RL:
                if(muzzleflash && d->muzzle.x >= 0)
                    particle_flare(d->muzzle, d->muzzle, 250, PART_MUZZLE_FLASH2, 0xFFFFFF, 3.0f, d);
            case GUN_FIREBALL:
            case GUN_ICEBALL:
            case GUN_SLIMEBALL:
                pspeed = guns[gun].projspeed*4;
                if(d->type==ENT_AI) pspeed /= 2;
                newprojectile(from, to, (float)pspeed, local, id, d, gun);
                break;

            case GUN_GL:
            {
                float dist = from.dist(to);
                vec up = to;
                up.z += dist/8;
                if(muzzleflash && d->muzzle.x >= 0)
                    particle_flare(d->muzzle, d->muzzle, 200, PART_MUZZLE_FLASH2, 0xFFFFFF, 1.5f, d);
                if(muzzlelight) adddynlight(hudgunorigin(gun, d->o, to, d), 20, vec(0.5f, 0.375f, 0.25f), 100, 100, DL_FLASH, 0, vec(0, 0, 0), d);
                newbouncer(from, up, local, id, d, BNC_GRENADE, 2000, 200);
                break;
            }

            case GUN_BOMB:
            {
                // float dist = from.dist(to);
                vec up = to;
                vec src(from);
                float f = 5.0f, dx = to.x-from.x, dy = to.y-from.y, dyy = 15.0f, dzz = 2.0f;
                src.add(vec(dx/f,dy/f,-dzz));
                up.add(vec(0, dyy, -dzz*f));
                if(muzzleflash && d->muzzle.x >= 0)
                    particle_flare(d->muzzle, d->muzzle, 500, PART_MUZZLE_FLASH2, 0xFFFFFF, 2.3f, d);
                if(muzzlelight) adddynlight(hudgunorigin(gun, d->o, to, d), 40, vec(0.5f, 0.375f, 0.25f), 100, 100, DL_FLASH, 0, vec(0, 0, 0), d);
                newbouncer(src, up, local, id, d, BNC_BOMB, 5500-(d->bombdelay*500), 20);
                break;
            }

            case GUN_RIFLE:
                particle_splash(PART_SPARK, 200, 250, to, 0xB49B4B, 0.24f);
                particle_trail(PART_SMOKE, 500, hudgunorigin(gun, from, to, d), to, 0x404040, 0.6f, 20);
                if(muzzleflash && d->muzzle.x >= 0)
                    particle_flare(d->muzzle, d->muzzle, 150, PART_MUZZLE_FLASH3, 0xFFFFFF, 1.25f, d);
                if(!local) adddecal(DECAL_BULLET, to, vec(from).sub(to).normalize(), 3.0f);
                if(muzzlelight) adddynlight(hudgunorigin(gun, d->o, to, d), 25, vec(0.5f, 0.375f, 0.25f), 75, 75, DL_FLASH, 0, vec(0, 0, 0), d);
                break;
        }

        bool looped = false;
        if(d->attacksound >= 0 && d->attacksound != sound) d->stopattacksound();
        if(d->idlesound >= 0) d->stopidlesound();
        switch(sound)
        {
            case S_CHAINSAW_ATTACK:
                if(d->attacksound >= 0) looped = true;
                d->attacksound = sound;
                d->attackchan = playsound(sound, d==hudplayer() ? NULL : &d->o, NULL, -1, 100, d->attackchan);
                break;
            default:
                playsound(sound, d==hudplayer() ? NULL : &d->o);
                break;
        }
        if(d->quadmillis && lastmillis-prevaction>200 && !looped) playsound(S_ITEMPUP, d==hudplayer() ? NULL : &d->o);
    }

    void particletrack(physent *owner, vec &o, vec &d)
    {
        if(owner->type!=ENT_PLAYER && owner->type!=ENT_AI) return;
        fpsent *pl = (fpsent *)owner;
        if(pl->muzzle.x < 0 || pl->lastattackgun != pl->gunselect) return;
        float dist = o.dist(d);
        o = pl->muzzle;
        if(dist <= 0) d = o;
        else
        {
            vecfromyawpitch(owner->yaw, owner->pitch, 1, 0, d);
            float newdist = raycube(owner->o, d, dist, RAY_CLIPMAT|RAY_ALPHAPOLY);
            d.mul(min(newdist, dist)).add(owner->o);
        }
    }

    void dynlighttrack(physent *owner, vec &o, vec &hud)
    {
        if(owner->type!=ENT_PLAYER && owner->type!=ENT_AI) return;
        fpsent *pl = (fpsent *)owner;
        if(pl->muzzle.x < 0 || pl->lastattackgun != pl->gunselect) return;
        o = pl->muzzle;
        hud = owner == hudplayer() ? vec(pl->o).add(vec(0, 0, 2)) : pl->muzzle;
    }

    float intersectdist = 1e16f;

    bool intersect(dynent *d, const vec &from, const vec &to, float &dist)   // if lineseg hits entity bounding box
    {
        vec bottom(d->o), top(d->o);
        bottom.z -= d->eyeheight;
        top.z += d->aboveeye;
        return linecylinderintersect(from, to, bottom, top, d->radius, dist);
    }

    dynent *intersectclosest(const vec &from, const vec &to, fpsent *at, float &bestdist)
    {
        dynent *best = NULL;
        bestdist = 1e16f;
        loopi(numdynents())
        {
            dynent *o = iterdynents(i);
            if(o==at || o->state!=CS_ALIVE) continue;
            float dist;
            if(!intersect(o, from, to, dist)) continue;
            if(dist<bestdist)
            {
                best = o;
                bestdist = dist;
            }
        }
        return best;
    }

    void shorten(vec &from, vec &target, float dist)
    {
        target.sub(from).mul(min(1.0f, dist)).add(from);
    }

    void raydamage(vec &from, vec &to, fpsent *d)
    {
        int qdam = guns[d->gunselect].damage;
        if(d->quadmillis) qdam *= 4;
        if(d->type==ENT_AI) qdam /= MONSTERDAMAGEFACTOR;
        dynent *o;
        float dist;
        if(d->gunselect==GUN_SG)
        {
            dynent *hits[SGRAYS];
            loopi(SGRAYS) 
            {
                if((hits[i] = intersectclosest(from, sg[i], d, dist))) shorten(from, sg[i], dist);
                else adddecal(DECAL_BULLET, sg[i], vec(from).sub(sg[i]).normalize(), 2.0f);
            }
            loopi(SGRAYS) if(hits[i])
            {
                o = hits[i];
                hits[i] = NULL;
                int numhits = 1;
                for(int j = i+1; j < SGRAYS; j++) if(hits[j] == o)
                {
                    hits[j] = NULL;
                    numhits++;
                }
                hitpush(numhits*qdam, o, d, from, to, d->gunselect, numhits);
            }
        }
        else if((o = intersectclosest(from, to, d, dist)))
        {
            shorten(from, to, dist);
            hitpush(qdam, o, d, from, to, d->gunselect, 1);
        }
        else if(d->gunselect!=GUN_FIST && d->gunselect!=GUN_BITE) adddecal(DECAL_BULLET, to, vec(from).sub(to).normalize(), d->gunselect==GUN_RIFLE ? 3.0f : 2.0f);
    }

    void shoot(fpsent *d, const vec &targ)
    {
        int prevaction = d->lastaction, attacktime = lastmillis-prevaction;
        if(attacktime<d->gunwait) return;
        d->gunwait = 0;
        if((d==player1 || d->ai) && !d->attacking) return;
        d->lastaction = lastmillis;
        d->lastattackgun = d->gunselect;
        if(!d->ammo[d->gunselect])
        {
            if(d==player1)
            {
                msgsound(S_NOAMMO, d);
                d->gunwait = 600;
                d->lastattackgun = -1;
                if (d->gunselect != GUN_BOMB) weaponswitch(d); // only switch if current weapon is not the bomb
                // weaponswitch(d);
            }
            return;
        }
        if(d->gunselect) d->ammo[d->gunselect]--;
        vec from = d->o;
        vec to = targ;

        vec unitv;
        float dist = to.dist(from, unitv);
        unitv.div(dist);
        vec kickback(unitv);
        kickback.mul(guns[d->gunselect].kickamount*-2.5f);
        d->vel.add(kickback);
        float shorten = 0;
        if(guns[d->gunselect].range && dist > guns[d->gunselect].range)
            shorten = guns[d->gunselect].range;
        float barrier = raycube(d->o, unitv, dist, RAY_CLIPMAT|RAY_ALPHAPOLY);
        if(barrier > 0 && barrier < dist && (!shorten || barrier < shorten))
            shorten = barrier;
        if(shorten) to = vec(unitv).mul(shorten).add(from);

        if(d->gunselect==GUN_SG) createrays(from, to);
        else if(d->gunselect==GUN_CG) offsetray(from, to, 1, guns[GUN_CG].range, to);

        hits.setsize(0);

        if(!guns[d->gunselect].projspeed) raydamage(from, to, d);

        shoteffects(d->gunselect, from, to, d, true, 0, prevaction);

        if(d==player1 || d->ai)
        {
            addmsg(N_SHOOT, "rci2i6iv", d, lastmillis-maptime, d->gunselect,
                   (int)(from.x*DMF), (int)(from.y*DMF), (int)(from.z*DMF),
                   (int)(to.x*DMF), (int)(to.y*DMF), (int)(to.z*DMF),
                   hits.length(), hits.length()*sizeof(hitmsg)/sizeof(int), hits.getbuf());
        }

		d->gunwait = guns[d->gunselect].attackdelay;
		if(d->gunselect == GUN_PISTOL && d->ai) d->gunwait += int(d->gunwait*(((101-d->skill)+rnd(111-d->skill))/100.f));
        d->totalshots += guns[d->gunselect].damage*(d->quadmillis ? 4 : 1)*(d->gunselect==GUN_SG ? SGRAYS : 1);
    }

    void adddynlights()
    {
        loopv(projs)
        {
            projectile &p = projs[i];
            if(p.gun!=GUN_RL) continue;
            vec pos(p.o);
            pos.add(vec(p.offset).mul(p.offsetmillis/float(OFFSETMILLIS)));
            adddynlight(pos, RL_DAMRAD/2, vec(1, 0.75f, 0.5f));
        }
        loopv(bouncers)
        {
            bouncer &bnc = *bouncers[i];
            if(bnc.bouncetype!=BNC_GRENADE && bnc.bouncetype!=BNC_BOMB) continue;
            vec pos(bnc.o);
            pos.add(vec(bnc.offset).mul(bnc.offsetmillis/float(OFFSETMILLIS)));
            if(bnc.bouncetype==BNC_GRENADE) adddynlight(pos, 8, vec(0.25f, 1, 1));
            else if (bnc.bouncetype==BNC_BOMB) adddynlight(pos, 8, vec(1, 1, 0.25f));
        }
    }

    static const char * const projnames[3] = { "projectiles/grenade", "projectiles/rocket", "projectiles/bomb" };
    static const char * const gibnames[3] = { "gibs/gib01", "gibs/gib02", "gibs/gib03" };
    static const char * const debrisnames[4] = { "debris/debris01", "debris/debris02", "debris/debris03", "debris/debris04" };
    static const char * const barreldebrisnames[4] = { "barreldebris/debris01", "barreldebris/debris02", "barreldebris/debris03", "barreldebris/debris04" };
         
    void preloadbouncers()
    {
        loopi(sizeof(projnames)/sizeof(projnames[0])) preloadmodel(projnames[i]);
        loopi(sizeof(gibnames)/sizeof(gibnames[0])) preloadmodel(gibnames[i]);
        loopi(sizeof(debrisnames)/sizeof(debrisnames[0])) preloadmodel(debrisnames[i]);
        loopi(sizeof(barreldebrisnames)/sizeof(barreldebrisnames[0])) preloadmodel(barreldebrisnames[i]);
    }

// Static values for the bomb barrier particles

#define bbarr_type 8
#define bbarr_dir 3
#define bbarr_num 1
#define bbarr_fade 2000
#define bbarr_size 1.2f //0.8f
#define bbarr_gravity 0
#define bbarr_overlap 4.0f
#define bbarr_height 5.0f
#define bbarr_color 0X2E7187 //0x2E7187 //0x0084AD //0x00E22D
#define bbarr_tremblepeak 7

// Mapvars

    VARP(bombcolliderad, 0, 16, 1000);
    VARP(bombbarrier, 0, 1, 1);
	
    void renderbouncers()
    {
        float yaw, pitch;
        loopv(bouncers)
        {
            bouncer &bnc = *bouncers[i];
            vec pos(bnc.o);
            pos.add(vec(bnc.offset).mul(bnc.offsetmillis/float(OFFSETMILLIS)));
            vec vel(bnc.vel);
            if(vel.magnitude() <= 25.0f) yaw = bnc.lastyaw;
            else
            {
                vectoyawpitch(vel, yaw, pitch);
                yaw += 90;
                bnc.lastyaw = yaw;
            }
            pitch = -bnc.roll;
            if(bnc.bouncetype==BNC_GRENADE)
                rendermodel(&bnc.light, "projectiles/grenade", ANIM_MAPMODEL|ANIM_LOOP, pos, yaw, pitch, MDL_CULL_VFC|MDL_CULL_OCCLUDED|MDL_LIGHT|MDL_DYNSHADOW);
            else if(bnc.bouncetype==BNC_BOMB) // Render BOMB projectile
            {
                vec mov_from(0, 0, bombcolliderad-bbarr_overlap);                                                // shift the lower part of the Barrier upwards
                vec mov_to(0, 0, -bombcolliderad+bbarr_height);                                                  // shift the upper part downwards
                vec floor = bnc.o; floor.z = floor.z - raycube(floor, vec(0, 0, -1), 0.2f, RAY_CLIPMAT);         // Get the rays on the floor.
                int tremble = (rnd(bbarr_tremblepeak*2)) - bbarr_tremblepeak;                                    // Compute random tremble

                if(bombbarrier)
                    regularshape(bbarr_type, bombcolliderad + tremble, bbarr_color, bbarr_dir, bbarr_num, bbarr_fade, floor, bbarr_size, bbarr_gravity, &mov_from, &mov_to);
                rendermodel(&bnc.light, "projectiles/bomb", ANIM_MAPMODEL|ANIM_LOOP, pos, yaw, pitch, MDL_CULL_VFC|MDL_CULL_OCCLUDED|MDL_LIGHT|MDL_DYNSHADOW);
            }
            // DEBUG: vvv REMOVE RENDERMODEL FOR SPLINTERS vvv
            /*
            else if(bnc.bouncetype==BNC_SPLINTER)
            {
                rendermodel(&bnc.light, "projectiles/grenade", ANIM_MAPMODEL|ANIM_LOOP, pos, yaw, pitch, MDL_CULL_VFC|MDL_CULL_OCCLUDED|MDL_LIGHT|MDL_DYNSHADOW);
            }
            */
            // TODO: ^^^ REMOVE RENDERMODEL FOR SPLINTERS ^^^
            else
            {
                const char *mdl = NULL;
                int cull = MDL_CULL_VFC|MDL_CULL_DIST|MDL_CULL_OCCLUDED;
                float fade = 1;
                if(bnc.lifetime < 250) fade = bnc.lifetime/250.0f;
                switch(bnc.bouncetype)
                {
                    case BNC_GIBS: mdl = gibnames[bnc.variant]; cull |= MDL_LIGHT|MDL_LIGHT_FAST|MDL_DYNSHADOW; break;
                    case BNC_DEBRIS: mdl = debrisnames[bnc.variant]; break;
                    case BNC_BARRELDEBRIS: mdl = barreldebrisnames[bnc.variant]; break;
                    default: continue;
                }
                rendermodel(&bnc.light, mdl, ANIM_MAPMODEL|ANIM_LOOP, pos, yaw, pitch, cull, NULL, NULL, 0, 0, fade);
            }
        }
    }

    void renderprojectiles()
    {
        float yaw, pitch;
        loopv(projs)
        {
            projectile &p = projs[i];
            if(p.gun!=GUN_RL) continue;
            vec pos(p.o);
            pos.add(vec(p.offset).mul(p.offsetmillis/float(OFFSETMILLIS)));
            if(p.to==pos) continue;
            vec v(p.to);
            v.sub(pos);
            v.normalize();
            // the amount of distance in front of the smoke trail needs to change if the model does
            vectoyawpitch(v, yaw, pitch);
            yaw += 90;
            v.mul(3);
            v.add(pos);
            rendermodel(&p.light, "projectiles/rocket", ANIM_MAPMODEL|ANIM_LOOP, v, yaw, pitch, MDL_CULL_VFC|MDL_CULL_OCCLUDED|MDL_LIGHT);
        }
    }

    void checkattacksound(fpsent *d, bool local)
    {
        int gun = -1;
        switch(d->attacksound)
        {
            case S_CHAINSAW_ATTACK:
                if(chainsawhudgun) gun = GUN_FIST;
                break;
            default:
                return;
        }
        if(gun >= 0 && gun < NUMGUNS &&
           d->clientnum >= 0 && d->state == CS_ALIVE &&
           d->lastattackgun == gun && lastmillis - d->lastaction < guns[gun].attackdelay + 50)
        {
            d->attackchan = playsound(d->attacksound, local ? NULL : &d->o, NULL, -1, -1, d->attackchan);
            if(d->attackchan < 0) d->attacksound = -1;
        }
        else d->stopattacksound();
    }

    void checkidlesound(fpsent *d, bool local)
    {
        int sound = -1, radius = 0;
        if(d->clientnum >= 0 && d->state == CS_ALIVE) switch(d->gunselect)
        {
            case GUN_FIST:
                if(chainsawhudgun && d->attacksound < 0)
                {
                    sound = S_CHAINSAW_IDLE;
                    radius = 50;
                }
                break;
        }
        if(d->idlesound != sound)
        {
            if(d->idlesound >= 0) d->stopidlesound();
            if(sound >= 0)
            {
                d->idlechan = playsound(sound, local ? NULL : &d->o, NULL, -1, 100, d->idlechan, radius);
                if(d->idlechan >= 0) d->idlesound = sound;
            }
        }
        else if(sound >= 0)
        {
            d->idlechan = playsound(sound, local ? NULL : &d->o, NULL, -1, -1, d->idlechan, radius);
            if(d->idlechan < 0) d->idlesound = -1;
        }
    }

    void removeweapons(fpsent *d)
    {
        removebouncers(d);
        removeprojectiles(d);
    }

    void updateweapons(int curtime)
    {
        updateprojectiles(curtime);
        if(player1->clientnum>=0 && player1->state==CS_ALIVE) shoot(player1, worldpos); // only shoot when connected to server
        updatebouncers(curtime); // need to do this after the player shoots so grenades don't end up inside player's BB next frame
        fpsent *following = followingplayer();
        if(!following) following = player1;
        loopv(players)
        {
            fpsent *d = players[i];
            checkattacksound(d, d==following);
            checkidlesound(d, d==following);
        }
    }

    bool weaponcollide(physent *d, const vec &dir) {
        loopv(bouncers)
        {
           	bouncer *p = bouncers[i];
            if(p->bouncetype != BNC_BOMB) continue;
            if(!ellipsecollide(d, 
			       dir, p->o, vec(0, 0, 0), p->yaw, 
			       bombcolliderad, bombcolliderad, 
			       p->aboveeye, p->o.z - raycube(p->o, vec(0, 0, -1), 0.2f, RAY_CLIPMAT))) 
	      return false;
        }
        return true;
    }
	
	
    void avoidweapons(ai::avoidset &obstacles, float radius)
    {
        loopv(projs)
        {
            projectile &p = projs[i];
            obstacles.avoidnear(NULL, p.o.z + RL_DAMRAD + 1, p.o, radius + RL_DAMRAD);
        }
        loopv(bouncers)
        {
            bouncer &bnc = *bouncers[i];
            if(bnc.bouncetype != BNC_GRENADE && bnc.bouncetype != BNC_BOMB) continue;
            obstacles.avoidnear(NULL, bnc.o.z + RL_DAMRAD + 1, bnc.o, radius + RL_DAMRAD);
        }
    }
};

