/**
 * @file JWTHandler.h
 * @brief Creation and validation of JSON Web Tokens (HS256).
 * @since 0.1.0
 */

#pragma once

#include <chrono>
#include <map>
#include <string>
#include <vector>

#include "TK4CPP/JWT/Export.h"

namespace TK4CPP
{
	namespace JWT
	{
		/**
		 * @class JWTHandler
		 * @brief A utility class for handling JSON Web Tokens (JWT).
		 */
		class TK4CPP_JWT_EXPORT JWTHandler final
		{
		public:
			/**
			 * @brief Sets the token duration.
			 * @param tokenDuration The duration for which the token is valid.
			 */
			static void setTokenDuration(std::chrono::duration<int> tokenDuration);

			/**
			 * @brief Validates a JWT token.
			 * @param token The JWT token to validate.
			 * @return True if the token is valid, false otherwise.
			 */
			static bool validate(const std::string& token);

			/**
			 * @brief Validates a JWT token with a specific issuer.
			 * @param token The JWT token to validate.
			 * @param issuer The expected issuer of the token.
			 * @return True if the token is valid and the issuer matches, false otherwise.
			 */
			static bool validate(const std::string& token, const std::string& issuer);

			/**
			 * @brief Validates a JWT token with specific claims.
			 * @param token The JWT token to validate.
			 * @param claims The expected claims in the token.
			 * @return True if the token is valid and the claims match, false otherwise.
			 */
			static bool validate(const std::string& token, const std::map<std::string, std::string>& claims);

			/**
			 * @brief Validates a JWT token with a specific issuer and claims.
			 * @param token The JWT token to validate.
			 * @param issuer The expected issuer of the token.
			 * @param claims The expected claims in the token.
			 * @return True if the token is valid, the issuer matches, and the claims match, false otherwise.
			 */
			static bool validate(const std::string& token, const std::string& issuer, const std::map<std::string, std::string>& claims);

			/**
			 * @brief Retrieves the user from a JWT token.
			 * @param token The JWT token.
			 * @return The username extracted from the token.
			 */
			static std::string getUser(const std::string& token);

		/*private:
			friend class AuthBase;*/
		public:
			/**
			 * @brief Creates a JWT token for a user with specific roles.
			 * @param username The username for which the token is created.
			 * @param roles The roles assigned to the user.
			 * @return The created JWT token.
			 */
			static std::string create(const std::string& username, const std::vector<std::string>& roles);

			/**
			 * @brief Creates a JWT token for a user with specific roles and token duration.
			 * @param username The username for which the token is created.
			 * @param roles The roles assigned to the user.
			 * @param tokenDuration The duration for which the token is valid.
			 * @return The created JWT token.
			 */
			static std::string create(const std::string& username, const std::vector<std::string>& roles, const std::chrono::duration<int>& tokenDuration);

		private:
			/**
			 * @brief The duration for which the token is valid.
			 */
			static std::chrono::duration<int> m_tokenDuration;
		};
	}
}