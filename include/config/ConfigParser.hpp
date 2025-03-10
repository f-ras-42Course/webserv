#ifndef CONFIG_PARSER_HPP
#define CONFIG_PARSER_HPP

#include "ConfigLexer.hpp"
#include "ConfigBuilder.hpp"
#include <memory>
#include <string>
#include <vector>

class ConfigParser {
public:
    // Parse exception with line and column information
    class ParseError : public std::runtime_error {
    public:
        ParseError(const std::string& msg, size_t line, size_t column)
            : std::runtime_error(formatError(msg, line, column))
            , line_(line)
            , column_(column) {}

        size_t getLine() const { return line_; }
        size_t getColumn() const { return column_; }

    private:
        static std::string formatError(const std::string& msg, size_t line, size_t column) {
            return "Line " + std::to_string(line) + ", Column " + 
                   std::to_string(column) + ": " + msg;
        }

        size_t line_;
        size_t column_;
    };

    // Parse from input stream
    static std::unique_ptr<Config> parse(std::istream& input);

private:
    ConfigParser(ConfigLexer& lexer) : lexer_(lexer) {}

    // Main parsing methods
    std::unique_ptr<Config> parseConfig();
    void parseServerBlock(ConfigBuilder& builder);
    void parseLocationBlock(ConfigBuilder& builder);
    void parseDirective(ConfigBuilder& builder, bool in_location);
    void parseReturn(ConfigBuilder& builder);  // New method for return directive

    // Helper methods
    Token getCurrentToken() const { return current_token_; }
    void advance();
    bool match(TokenType type);
    void expect(TokenType type, const std::string& error_msg);
    std::string expectIdentifier(const std::string& error_msg);
    std::vector<std::string> expectIdentifierList(const std::string& error_msg);
    uint64_t expectNumber(const std::string& error_msg);

    // Member variables
    ConfigLexer& lexer_;
    Token current_token_{TokenType::INVALID, "", 0, 0};
};

#endif