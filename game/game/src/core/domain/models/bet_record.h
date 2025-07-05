#ifndef BET_RECORD_H
#define BET_RECORD_H

#include "core/domain/interfaces/i_bet.h"
#include <string>

class BetRecord
{
public:
	BetRecord() : playType_(""), amount_(0.0) {}
                
	BetRecord(const std::string& playType, double amount)
	   : playType_(playType), amount_(amount) {}

	// 基本投注信息
	const std::string& getPlayType() const { return playType_; }
	double getAmount() const { return amount_; }

	void setPlayType(const std::string& playType) { playType_ = playType; }
	void setAmount(double amount) { amount_ = amount; }

private:
	std::string playType_;  // 下注类型
	double amount_;         // 下注金额
};

#endif // BET_RECORD_H