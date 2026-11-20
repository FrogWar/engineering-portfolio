#ifndef PLAYER_H
#define PLAYER_H
#include "Character.h"
#include <ostream>

class Player : public Character {
protected:
    int potions;
public:
    Player(const std::string& n, int hp, int atk, int en = 100, int pots = 2);
    virtual ~Player() = default;
    int usePotion();
    int getPotions() const;
    void addPotion(int amount);
    Player& operator+=(int amount);
    bool operator==(const Player& other) const;
    friend std::ostream& operator<<(std::ostream& os, const Player& p);
};

class Warrior : public Player {
public:
    explicit Warrior(const std::string& n);
    int basicAttack(Character& target) override;
    int specialMove(Character& target) override;
    std::string getClassName() const override;
};

class Mage : public Player {
public:
    explicit Mage(const std::string& n);
    int basicAttack(Character& target) override;
    int specialMove(Character& target) override;
    std::string getClassName() const override;
};

class Healer : public Player {
public:
    explicit Healer(const std::string& n);
    int basicAttack(Character& target) override;
    int specialMove(Character& target) override;
    std::string getClassName() const override;
};

#endif
