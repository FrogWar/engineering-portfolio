#ifndef BOSS_H
#define BOSS_H
#include "Character.h"
#include <string>

class Boss : public Character {
private:
    int rage;
public:
    Boss(const std::string& n);
    int basicAttack(Character& target) override;
    int specialMove(Character& target) override;
    std::string getClassName() const override;
    int getRage() const;
    void addRage(int amount);
    std::string chooseAttackName() const;
};

#endif
