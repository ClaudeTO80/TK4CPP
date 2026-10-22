#include "TK4CPP/JWT/JWTHandler.h"

#include <algorithm>

#include "jwt-cpp/jwt.h"

using namespace TK4CPP;
using namespace TK4CPP::JWT;

std::chrono::duration<int> JWTHandler::m_tokenDuration{ std::chrono::hours(24) };

void JWTHandler::setTokenDuration(std::chrono::duration<int>  tokenDuration)
{
	m_tokenDuration = tokenDuration;
}

std::string JWTHandler::create(const std::string& username, const std::vector<std::string>& roles)
{
	return create(username, roles, m_tokenDuration);
}

std::string JWTHandler::create(const std::string& username, const std::vector<std::string>& roles, const std::chrono::duration<int>& tokenDuration)
{
	const std::chrono::time_point<std::chrono::system_clock> now = std::chrono::system_clock::now();
	const std::chrono::time_point<std::chrono::system_clock> expireTime = now + tokenDuration;

	jwt::traits::kazuho_picojson::string_type token = 
			jwt::create()
				.set_type("JWS")
				.set_not_before(now)
				.set_expires_at(expireTime)
				.set_issuer("auth0")
				.set_payload_claim("user", jwt::claim(username))
				.set_payload_claim("roles", jwt::claim(roles.begin(),roles.end()))
				.sign(jwt::algorithm::hs256{ "HiAll" });

	return token;
}

bool JWTHandler::validate(const std::string& token)
{
	return validate(token, "", {});
}

bool JWTHandler::validate(const std::string& token, const std::string& issuer)
{
	return validate(token, issuer, {});
}

bool JWTHandler::validate(const std::string& token, const std::map<std::string, std::string>& claims)
{
	return validate(token, "", claims);
}

bool JWTHandler::validate(const std::string& token, const std::string& issuer, const std::map<std::string,std::string>& claims)
{
	auto verifier = jwt::verify();

	if (!issuer.empty())
		verifier = verifier.with_issuer(issuer);

	std::for_each(claims.begin(), claims.end(), [&](std::pair<std::string,std::string> curr) {
		verifier = verifier.with_claim(curr.first, jwt::claim(std::string(curr.second)));
		});

	verifier= verifier.allow_algorithm(jwt::algorithm::hs256{ "HiAll" });

	try
	{
		verifier.verify(jwt::decode(token));
		return true;
	} 

	catch (const jwt::error::rsa_exception& ) {}
	catch (const jwt::error::ecdsa_exception& ) {}
	catch (const jwt::error::signature_verification_exception& ) {}
	catch (const jwt::error::signature_generation_exception& ) {}
	catch (const jwt::error::token_verification_exception& ) {}
	catch (const std::exception& ) {}

	return false;
}

std::string JWTHandler::getUser(const std::string& token)
{
	static std::string empty("");
	return		validate(token)
			?	jwt::decode(token).get_payload_claim("user").as_string()
			:	empty;

}

