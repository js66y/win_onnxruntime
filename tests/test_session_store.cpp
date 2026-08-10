#include <filesystem>

#include <gtest/gtest.h>

#include "storage/session_store.h"

namespace {

namespace fs = std::filesystem;

class SessionStoreTest : public ::testing::Test {
 protected:
  void SetUp() override {
    dir_ = fs::temp_directory_path() / "echo_store_test";
    fs::create_directories(dir_);
    db_ = dir_ / "test.db";
  }
  void TearDown() override { fs::remove_all(dir_); }

  fs::path dir_;
  fs::path db_;
};

TEST_F(SessionStoreTest, CreateAppendAndList) {
  auto store = echo::storage::SessionStore::Open(db_);
  ASSERT_TRUE(store.has_value()) << store.error().message;

  auto session = (*store)->CreateSession("echo");
  ASSERT_TRUE(session.has_value()) << session.error().message;

  ASSERT_TRUE((*store)->AppendMessage(session->id, "user", "你好"));
  ASSERT_TRUE((*store)->AppendMessage(session->id, "assistant", "你好呀"));

  auto messages = (*store)->ListMessages(session->id);
  ASSERT_TRUE(messages.has_value());
  ASSERT_EQ(messages->size(), 2u);
  EXPECT_EQ((*messages)[0].role, "user");
  EXPECT_EQ((*messages)[0].content, "你好");
  EXPECT_EQ((*messages)[1].content, "你好呀");

  auto sessions = (*store)->ListSessions();
  ASSERT_TRUE(sessions.has_value());
  ASSERT_GE(sessions->size(), 1u);
  EXPECT_NE((*sessions)[0].title.find("你好"), std::string::npos);
}

TEST_F(SessionStoreTest, SetRole) {
  auto store = echo::storage::SessionStore::Open(db_);
  ASSERT_TRUE(store.has_value());
  auto session = (*store)->CreateSession("echo");
  ASSERT_TRUE(session.has_value());
  ASSERT_TRUE((*store)->SetSessionRole(session->id, "tutor"));

  auto sessions = (*store)->ListSessions();
  ASSERT_TRUE(sessions.has_value());
  EXPECT_EQ((*sessions)[0].role_id, "tutor");
}

}  // namespace
