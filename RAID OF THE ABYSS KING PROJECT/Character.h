#ifndef CHARACTER_H
#define CHARACTER_H
#include <string>
#include <stdexcept>

class Character {
protected:
    std::string name;
    int health;
    int maxHealth;
    int energy;
    int attackPower;
    bool defending;
public:
    Character(const std::string& n, int hp, int atk, int en = 100);
    virtual ~Character() = default;
    virtual int basicAttack(Character& target) = 0; 
    virtual int specialMove(Character& target) = 0;
    virtual std::string getClassName() const = 0;
    virtual void takeDamage(int amount);
    virtual void heal(int amount);
    bool isAlive() const;
    void setDefending(bool value);
    bool isDefending() const;
    const std::string& getName() const;
    int getHealth() const;
    int getMaxHealth() const;
    int getEnergy() const;
    int getAttackPower() const;
    void addEnergy(int amount);
    void consumeEnergy(int amount);
};

#endif
