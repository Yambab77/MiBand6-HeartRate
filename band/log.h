#pragma once

#include <string>

// ��ʼ��/�رգ�����/ֹͣ��̨ˢ���̣߳�׼����־Ŀ¼�뵱�� HR ��־��
void Log_Init();
void Log_Shutdown();

// �����Ƿ�ͬʱ���������̨���� InitConsole ���ã�
void Log_SetConsoleEnabled(bool enabled);

// ��¼һ����־���Զ�������������־��Ӧ����־��
// Լ�������� "[ADV-HR]" ����д�뵱�� HR ��־������д�� app.log
void AppendLog(const std::wstring& wline);
